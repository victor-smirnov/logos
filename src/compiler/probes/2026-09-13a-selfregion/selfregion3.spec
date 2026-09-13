name: implanonx
file: src/compiler/sema_collect.cpp
---
            self_type = target_resolved;
        if (self_type)
            current_type_params_["Self"] = self_type;
    }
    // Verify trait exists (only for trait impls)
    // Copy and Drop are built-in marker traits — not always visible through
    // the dependency-graph (pub trait + use isn't enough when the target
    // type's own package re-imports a different non-pub Drop, e.g. std.string
    // and writ.zone both used to declare local `trait Drop`). Treating Drop
    // as a built-in matches Copy and lets the impl resolve via name alone.
    const bool builtin_marker_ = !trait_name.empty() &&
        !(trait_name != "Copy" && trait_name != "Drop");
    const bool trait_is_drop_ = builtin_marker_ && trait_name != "Copy";
    if (!trait_name.empty() && !builtin_marker_ && !traits_.count(trait_name))
        error(std::format("impl: unknown trait '{}'", trait_name));
    // rustc check_drop_impl: E0120 / E0366 / E0367 at the declaration. PROBES.md 2026-09-02u.
    if (trait_is_drop_) check_drop_impl_wf(target, target_resolved, impl_tps, node);
    // Phase 6: scope the impl's trait name so `Self::Item<X>` inside
    // method bodies / signatures resolves before impls_ is populated.
    current_impl_trait_name_ = trait_name;
    // Resolve the trait's PACKAGE once, through the reader that already
    // implements Rust's shadowing order (cur_package_::Name first). A user
    // `trait Drop` in this package resolves to THIS package; the prelude's
    // resolves to logos.lang.drop. Empty when the name resolves to nothing
    // (builtin_marker_ lets `Drop`/`Copy` impls through with no declaration).
    current_impl_trait_package_.clear();
    if (!trait_name.empty()) {
        auto tit_ = find_trait_iter_scoped(trait_name);
        if (tit_ != traits_.end()) current_impl_trait_package_ = tit_->second.package;
    }
    // Resolve trait type args (e.g. impl Into<i32> for Celsius → T=i32)
    // and push them into current_type_params_ so method sigs resolve correctly.
    std::vector<TypeRef> trait_type_args;
    // B62: parallel collection of lifetime args at trait position
    // (`impl Trait<'a, T>` → ["a"]). Skipped from type_args resolution but
    // captured for HRTB satisfaction check at bound time.
    std::vector<std::string> trait_lt_args;
    if (!trait_name.empty() && node.has_key(la::TYPE_PARAMS)) {
        AnyVal tpav = node.get(la::TYPE_PARAMS.code);
        if (!tpav.is_null()) {
            auto tplist = map_of(tpav);
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i) {
                    auto item = map_of(items.get(i));
                    // L1: skip LIFETIME_PARAM entries — they're lifetime
                    // arg position in `impl Foo<'a> for ...`. Logos doesn't
                    // track regions structurally for trait dispatch, and
                    // resolve_type would error on code 131 (LIFETIME_PARAM).
                    if (code_of(item) == la::LIFETIME_PARAM) {
                        trait_lt_args.push_back(
                            std::string(str_of(item.get(la::NAME.code))));
                        continue;
                    }
                    trait_type_args.push_back(resolve_type(item));
                }
            }
        }
        auto tit = find_trait_iter_scoped(trait_name);
        if (tit != traits_.end()) {
            for (size_t i = 0; i < tit->second.type_params.size() &&
                                i < trait_type_args.size(); ++i)
                current_type_params_[tit->second.type_params[i].name] = trait_type_args[i];
        }
    }
    // G156-1: expose this impl's concrete trait type-args to collect_fn so the
    // method collision-detection can mangle by them (empty for inherent impls).
    current_impl_trait_args_ = trait_type_args;
    // Detect blanket impl: `impl<T: Bound> Trait for T` — target IS one of
    // this impl's own type parameters.  Methods are collected as generic fns
    // (target becomes the TypeVar name, e.g. "T__method"); later, at call
    // sites on concrete types satisfying Bound, we instantiate the blanket.
    bool is_blanket = false;
    std::string blanket_bound_trait;
    std::vector<std::string> blanket_extra_bounds;
    // B-mv-03: identities parallel to the two above (see BlanketImpl).
    // `blanket_trait_canonical` is the identity of the trait BEING implemented;
    // resolved here rather than reusing `coh_trait` (computed at the bottom of
    // this function) only because the pushes below happen first. Same call, same
    // scope, so the two agree by construction — `check_impl_registry_key_identity`
    // is what makes "by construction" mechanical rather than asserted.
    std::string blanket_trait_canonical =
        trait_name.empty() ? std::string() : canonical_trait_name(trait_name);
    std::string blanket_bound_canonical;
    std::vector<std::string> blanket_extra_canonical;
    // ADR 0008: associated-type equality clauses parallel to the bound list.
    std::vector<std::pair<std::string, TypeRef>> blanket_primary_assoc_eqs;
    std::vector<std::pair<std::string,
        std::vector<std::pair<std::string, TypeRef>>>> blanket_extra_assoc_eqs;
    if (!trait_name.empty()) {
        for (auto& tp : impl_tps) {
            if (tp.name == target) {
                is_blanket = true;
                if (!tp.bounds.empty()) {
                    blanket_bound_trait = tp.bounds[0].trait_name;
                    // B-mv-03: the bound's IDENTITY, already captured on the
                    // TraitBound at read time (read_trait_bound_args) in THIS
                    // impl's declaring scope. THIS is the string the blanket
                    // walk asks the registry with — `impl<T: Hash> Marker for T`
                    // written next to a package-local `trait Hash` must admit
                    // that Hash's concretes and not the stdlib Hash's.
                    blanket_bound_canonical = tp.bounds[0].canonical_trait;
                    blanket_primary_assoc_eqs = tp.bounds[0].assoc_eqs;
                    for (size_t bi = 1; bi < tp.bounds.size(); ++bi) {
                        blanket_extra_bounds.push_back(tp.bounds[bi].trait_name);
                        blanket_extra_canonical.push_back(tp.bounds[bi].canonical_trait);
                        blanket_extra_assoc_eqs.emplace_back(
                            tp.bounds[bi].trait_name, tp.bounds[bi].assoc_eqs);
                    }
                }
                break;
            }
        }
    }

    // Snapshot blanket_impls_ size so we can detect after the items loop
    // whether *any* per-method blanket entry got pushed. Blankets that
    // declare only assoc-types (no fn methods) need a marker entry so
    // trait-satisfaction queries can find them — without it,
    // sema_has_impl_recursive would report the blanket trait as
    // unsatisfied for any concrete that depends on it.
    size_t blanket_size_before = blanket_impls_.size();

    // Register impl methods as free functions with mangled names: Target__method
    // Also collect associated type definitions.
    // Skip if already registered (e.g. class methods defined inline).
    if (node.has_key(la::ITEMS)) {
        auto items = arr_of(node.get(la::ITEMS.code));
        // Phase A.2: sweep doc-lines into pending_doc_ so the next
        // collect_fn invocation picks them up via take_pending_doc().
        pending_doc_.clear();
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto m = map_of(items.get(i));
            if (try_append_doc(pending_doc_, m)) continue;
            if (code_of(m) == la::REL_OP) {
                // ADR 0024 S6: `op entry.key eq = __ctr_at_Hs…  exact;` — ONE
                // access operation the source publishes. Collected onto the
                // rel binding it names, so a planner reads what the source
                // SAYS rather than guessing from a materializer's name.
                std::string lead(str_of(m.get(la::REL_KW.code)));
                if (lead != "op") {
                    error(std::format(
                        "impl for '{}': unexpected member '{} …' — an access "
                        "operation is written `op <rel>.<col> <cmp> = <fn> "
                        "[exact];`", target, lead));
                    continue;
                }
                SourceRelOp so;
                std::string rn(str_of(m.get(la::TYPE_NAME.code)));
                so.col = std::string(str_of(m.get(la::FIELD.code)));
                so.cmp = std::string(str_of(m.get(la::OP.code)));
                // NAME in both forms: a `#ident` antiquotation is resolved INTO
                // NAME before this ever collects.
                so.fn  = std::string(str_of(m.get(la::NAME.code)));
                std::string flag(m.has_key(la::RET_TYPE)
                                 ? std::string(str_of(m.get(la::RET_TYPE.code)))
                                 : std::string());
                if (!flag.empty() && flag != "exact") {
                    error(std::format(
                        "impl for '{}': `op {}.{}` — unknown flag '{}'; the only "
                        "one is `exact` (absent = the operation returns a "
                        "SUPERSET and the query keeps its filter)",
                        target, rn, so.col, flag));
                    continue;
                }
                so.exact = (flag == "exact");
                if (so.cmp != "eq" && so.cmp != "ge" && so.cmp != "le"
                    && so.cmp != "gt" && so.cmp != "lt") {
                    error(std::format(
                        "impl for '{}': `op {}.{}` answers comparison '{}' — "
                        "expected one of eq / ge / le / gt / lt",
                        target, rn, so.col, so.cmp));
                    continue;
                }
                bool bound_found = false;
                for (auto& e : source_impls_[target])
                    if (e.rel == rn) {
                        bool dup = false;
                        for (auto& prev : e.ops)
                            if (prev.col == so.col && prev.cmp == so.cmp) {
                                // Collect runs in several phases, so seeing the
                                // same operation again is CONFIRMATION — the
                                // same rule rel_bind already follows. Only a
                                // conflicting redeclaration is an error.
                                if (prev.fn != so.fn || prev.exact != so.exact)
                                    error(std::format(
                                        "impl for '{}': `op {}.{} {}` declared "
                                        "twice with different meanings ('{}'{} "
                                        "vs '{}'{}) — one operation per "
                                        "(column, comparison)",
                                        target, rn, so.col, so.cmp,
                                        prev.fn, prev.exact ? " exact" : "",
                                        so.fn, so.exact ? " exact" : ""));
                                dup = true;
                                break;
                            }
                        if (!dup) e.ops.push_back(std::move(so));
                        bound_found = true;
                        break;
                    }
                if (!bound_found)
                    error(std::format(
                        "impl for '{}': `op {}.{}` names no bound rel — declare "
                        "`rel {} = <materializer>;` in this impl first",
                        target, rn, so.col, rn));
                continue;
            }
            if (code_of(m) == la::REL_BIND) {
                // ADR 0016 §6: `rel edge = writ_graph_edges;` — bind one
                // trait rel to its native materializer for this type.
                std::string lead(str_of(m.get(la::REL_KW.code)));
                std::string rn(str_of(m.get(la::NAME.code)));
                if (lead == "size") {
                    // ADR 0024 S4 — `size <rel> = <fn>;`. The source's row
                    // count, reported at RUN time by a fn of the source alone.
                    // Shares rel_bind's SHAPE (`<lead> <name> = <fn>;`), which
                    // is what REL_KW is for: the lead ident is data, and the
                    // member is told apart by reading it rather than by a rule
                    // of its own. ⚠ The AST→source renderer had "rel " baked in
                    // for this node and now prints REL_KW — under `-g` a dump is
                    // REPARSED, so a lead the renderer cannot say becomes a
                    // duplicate rel binding.
                    std::string szfn(str_of(m.get(la::VALUE.code)));
                    bool rel_found = false;
                    for (auto& e : source_impls_[target])
                        if (e.rel == rn) {
                            // Collect runs in several phases: the same
                            // declaration seen again is confirmation, and only a
                            // conflicting one is an error — the rule `op` and
                            // `rel` already follow.
                            if (e.size_fn.empty()) e.size_fn = szfn;
                            else if (e.size_fn != szfn)
                                error(std::format(
                                    "impl for '{}': `size {}` declared twice with "
                                    "different reporters ('{}' vs '{}') — a "
                                    "relation has one size",
                                    target, rn, e.size_fn, szfn));
                            rel_found = true;
                            break;
                        }
                    if (!rel_found)
                        error(std::format(
                            "impl for '{}': `size {}` names no bound rel — "
                            "declare `rel {} = <materializer>;` in this impl "
                            "first", target, rn, rn));
                    continue;
                }
                if (lead == "order") {
                    // ADR 0025 S3 — `order <rel> = <col>;`. WHICH column the
                    // relation's rows already arrive sorted by. Shares
                    // rel_bind's shape for the same reason `size` does: the
                    // lead ident is data, so a third member needs no grammar
                    // rule and gets none.
                    //
                    // ⚠ ONLY THE COLUMN NAME IS CHECKED HERE, and the other
                    // half of the pairing is checked at spec time on purpose.
                    // "Is this column one the relation declares?" is answerable
                    // from `sig->cols`, which this pass already holds. "Is the
                    // producer's return type actually `OrderedBy`?" is a
                    // question about a fn's return type and the impl graph, and
                    // collect runs in several phases over a partially populated
                    // one — asking it here would answer `false` for a working
                    // impl collected in a later phase, which is a REFUSAL that
                    // fires on correct code. `native_source_spec` asks it where
                    // the answer is stable.
                    std::string ocol(str_of(m.get(la::VALUE.code)));
                    bool rel_found = false;
                    for (auto& e : source_impls_[target])
                        if (e.rel == rn) {
                            bool known = false;
                            for (const auto& c : e.cols)
                                if (c.name == ocol) { known = true; break; }
                            if (!known) {
                                std::string have;
                                for (const auto& c : e.cols) {
                                    if (!have.empty()) have += ", ";
                                    have += c.name;
                                }
                                error(std::format(
                                    "impl for '{}': `order {} = {}` names no "
                                    "column of rel '{}' — its columns are ({}). "
                                    "The ordered column must be one the "
                                    "relation publishes, or no query could name "
                                    "it in an `order by`",
                                    target, rn, ocol, rn, have));
                                rel_found = true;
                                break;
                            }
                            // Collect runs in several phases: the same
                            // declaration seen again is confirmation, and only
                            // a conflicting one is an error — the rule `op`,
                            // `rel` and `size` already follow.
                            if (e.ord_col.empty()) e.ord_col = ocol;
                            else if (e.ord_col != ocol)
                                error(std::format(
                                    "impl for '{}': `order {}` declared twice "
                                    "over different columns ('{}' vs '{}') — a "
                                    "relation arrives in ONE order",
                                    target, rn, e.ord_col, ocol));
                            rel_found = true;
                            break;
                        }
                    if (!rel_found)
                        error(std::format(
                            "impl for '{}': `order {}` names no bound rel — "
                            "declare `rel {} = <materializer>;` in this impl "
                            "first", target, rn, rn));
                    continue;
                }
                if (lead != "rel") {
                    error(std::format(
                        "impl for '{}': unexpected member '{} {} = …' — the "
                        "leads of this shape are `rel <r> = <materializer>;`, "
                        "`size <r> = <reporter>;` and `order <r> = <col>;`",
                        target, lead, rn));
                    continue;
                }
                if (trait_name.empty()) {
                    error(std::format(
                        "impl for '{}': `rel {} = …` outside a trait impl — "
                        "rel bindings implement a source trait's vocabulary "
                        "(`impl GraphSource for {} {{ … }}`)",
                        target, rn, target));
                    continue;
                }
                auto trit = trait_rels_.find(trait_name);
                const TraitRelSig* sig = nullptr;
                if (trit != trait_rels_.end())
                    for (const auto& ts : trit->second)
                        if (ts.rel == rn) { sig = &ts; break; }
                if (!sig) {
                    error(std::format(
                        "impl {} for {}: trait '{}' declares no rel '{}'",
                        trait_name, target, trait_name, rn));
                    continue;
                }
                SourceRelBind b;
                b.trait_name = trait_name;
                b.rel    = rn;
                b.mat_fn = std::string(str_of(m.get(la::VALUE.code)));
                b.mat_module = cur_package_;   // refined at spec time if needed
                b.cols   = sig->cols;
                bool dup = false, same = false;
                for (const auto& e : source_impls_[target])
                    if (e.rel == rn) {
                        dup = true;
                        // The convention pre-scan (#[derive_graph_source])
                        // seeds this exact binding a round before the derive's
                        // emitted impl collects — identical re-registration
                        // is confirmation, not conflict.
                        same = (e.trait_name == trait_name && e.mat_fn == b.mat_fn);
                        break;
                    }
                if (dup) {
                    if (!same)
                        error(std::format(
                            "impl {} for {}: duplicate rel binding '{}'",
                            trait_name, target, rn));
                    continue;
                }
                source_impls_[target].push_back(std::move(b));
                continue;
            }
            if (code_of(m) == la::FN || code_of(m) == la::STATIC_FN) {
                auto mname = std::string(str_of(m.get(la::NAME.code)));
                // Blanket impls use a synthetic target name so the method
                // doesn't collide with `T::method` lookups on other generic
                // `T: Trait` type parameters that share the same letter.
                // Blanket key includes the bound trait so distinct blankets
                // on the same trait (e.g. `impl<DT: Primitive> T for DT` and
                // `impl<DT: PodRef> T for DT`) register under separate keys.
                std::string reg_target = is_blanket
                    ? ("$blanket$" + trait_name + "$" + blanket_bound_trait + "$" + target)
                    : target;
                auto mangled = reg_target + "__" + mname;
                collect_fn(m, reg_target, trait_name);
                // The written `self:` type of an INHERENT impl method,
                // checked against the impl target for the first time here —
                // this is the only site that has both. (`lower_fn`'s `Self` is
                // a bare name with no arguments.) PROBES.md 2026-09-04a.
                if (trait_name.empty() && impl_self_ty && m.has_key(la::PARAMS)) {
                    auto pav = m.get(la::PARAMS.code);
                    if (pav.is_pointer()) {
                        auto pnode = map_of(pav);
                        if (pnode.has_key(la::ITEMS)) {
                            auto pitems = arr_of(pnode.get(la::ITEMS.code));
                            if (pitems.size() > 0) {
                                auto p0 = map_of(pitems.get(0));
                                if (code_of(p0) == la::PARAM && p0.has_key(la::NAME) &&
                                    str_of(p0.get(la::NAME.code)) == "self" &&
                                    p0.has_key(la::TYPE)) {
                                    TypeRef written = resolve_type(map_of(p0.get(la::TYPE.code)));
                                    if (written && (written.kind() == LogosType::Kind::Ref ||
                                               written.kind() == LogosType::Kind::MutRef))
                                        written = TypeRef(written.pointee());
                                    TypeRef self_ty{impl_self_ty};
                                    if (written) {
                                        bool eq = types_equal(written, self_ty);
                                        bool eq_lt = detail::types_equal_with_lifetimes(written, self_ty);
                                        auto adt_name = [](TypeRef t) {
                                            std::string s(t.struct_name());
                                            if (s.empty()) s = std::string(t.enum_name());
                                            return s;
                                        };
                                        std::string wname = adt_name(written), sname_ = adt_name(self_ty);
                                        bool same_adt = !wname.empty() && wname == sname_ &&
                                                        written.kind() == self_ty.kind();
                                        // Rust E0308 "mismatched `self`
                                        // parameter type". An ELIDED
                                        // lifetime-argument list asserts
                                        // nothing: `self: &C` inside
                                        // `impl<'a> C<'a>` is the elision, not
                                        // a second spelling — compare the
                                        // lifetime arguments only when some
                                        // were written. PROBES.md 2026-09-04a.
                                        bool lt_written = !written.lifetime_args().empty();
                                        if (same_adt && (!eq || (!eq_lt && lt_written)))
                                            error(std::format(
                                                "mismatched `self` parameter type: expected "
                                                "'{}', found '{}'",
                                                type_str(self_ty, true), type_str(written, true)));
                                    }
                                }
                            }
                        }
                    }
                }
                // Trait-impl methods inherit their trait's accessibility:
                // if the trait is reachable, so are its methods.  The
                // grammar disallows `pub fn` inside trait / trait-impl
                // blocks, so force is_pub=true post-collection.  Inherent
                // impls (no trait_name) keep the explicit pub/private split.
                if (!trait_name.empty()) {
                    // Push method-level type params so `fn m<H: Bound>(&self, x: &mut H)`
                    // can resolve H when re-walking params for public-visibility promotion.
                    auto method_tps = read_type_params(m);
                    if (!method_tps.empty()) push_type_params(method_tps);
                    std::vector<TypeRef> method_param_types;
                    if (m.has_key(la::PARAMS)) {
                        auto params_av = m.get(la::PARAMS.code);
                        if (params_av.is_pointer()) {
                            auto params_node = map_of(params_av);
                            if (params_node.has_key(la::ITEMS)) {
                                auto arr = arr_of(params_node.get(la::ITEMS.code));
                                for (uint64_t j = 0; j < arr.size(); ++j) {
                                    auto p = map_of(arr.get(j));
                                    if (code_of(p) != la::PARAM) continue;
                                    if (p.has_key(la::TYPE))
                                        method_param_types.push_back(resolve_type(map_of(p.get(la::TYPE.code))));
                                    else {
                                        auto self_t = current_type_params_.count("Self")
                                            ? current_type_params_.at("Self") : error_t();
                                        bool is_mut = p.has_key(la::IS_MUT) &&
                                                      !p.get(la::IS_MUT.code).is_null() &&
                                                      p.get(la::IS_MUT.code).as_value<uint8_t>() != 0;
                                        method_param_types.push_back(make_ref(is_mut, self_t));
                                    }
                                }
                            }
                        }
                    }
                    if (!method_tps.empty()) pop_type_params(method_tps);
                    if (auto it = find_func_by_base_and_signature(mangled, method_param_types, false))
                        const_cast<SemaFuncInfo*>(it)->is_pub = true;
                }
                if (is_blanket) {
                    BlanketImpl bi_rec;
                    bi_rec.trait_name = trait_name;
                    bi_rec.target_typevar = target;
                    bi_rec.bound_trait = blanket_bound_trait;
                    bi_rec.extra_bounds = blanket_extra_bounds;
                    // B-mv-03: trait IDENTITIES alongside the raw spellings.
                    bi_rec.canonical_trait = blanket_trait_canonical;
                    bi_rec.canonical_bound_trait = blanket_bound_canonical;
                    bi_rec.canonical_extra_bounds = blanket_extra_canonical;
                    bi_rec.method_name = mname;
                    bi_rec.mangled_name = mangled;
                    bi_rec.primary_assoc_eqs = blanket_primary_assoc_eqs;
                    bi_rec.extra_assoc_eqs = blanket_extra_assoc_eqs;
                    if (!cur_from_binary_) user_blanket_mangled_.insert(bi_rec.mangled_name);
                    blanket_impls_.push_back(std::move(bi_rec));
                }
            } else if (code_of(m) == la::ASSOC_TYPE_IMPL && !trait_name.empty()) {
                auto aname = std::string(str_of(m.get(la::NAME.code)));
                // For blanket impls, key under the synthetic `$blanket$...`
                // name so normal `T::Assoc` lookups on other generics don't
                // shadow; the AssocType resolver falls back to blanket keys
                // when the concrete base satisfies the blanket's bound.
                std::string key_target = is_blanket
                    ? ("$blanket$" + trait_name + "$" + blanket_bound_trait + "$" + target)
                    : target;
                // G156-1: key by the trait's concrete type-args so two impls of
                // a generic trait `Trait<T>` for ONE type at distinct T (each
                // declaring the same-named assoc type) coexist. The SUFFIXED key
                // (empty suffix for non-generic traits → identical to the legacy
                // key) is always stored; the PLAIN key is stored first-impl-wins
                // for backward-compat + unambiguous single-impl lookups, and
                // ERASED once a second distinct-args impl appears so a bare
                // ambiguous `X::Assoc` lookup fails loud (Rust requires
                // `<X as Trait<T>>::Assoc`). Resolution prefers the suffixed key
                // when the trait args are known from the impl context
                // (current_impl_trait_args_). A true duplicate (same
                // trait+args+target+name) still collides on the suffixed key.
                std::string targ_sfx = trait_targ_suffix(trait_type_args);
                std::string key  = trait_name + targ_sfx + "::" + key_target + "::" + aname;
                std::string pkey = trait_name + "::" + key_target + "::" + aname;
                if (assoc_type_impls_.count(key))
                    error(std::format("impl {} for {}: duplicate associated type '{}'",
                                      trait_name, target, aname));
                // GAT: read assoc type's own params (e.g. type Item<T> = ...)
                std::vector<TypeParam> gat_tps = read_type_params(m);
                // Bug 2 fix: GAT param names must not shadow impl type param names.
                for (auto& gtp : gat_tps)
                    for (auto& itp : impl_tps)
                        if (gtp.name == itp.name)
                            error(std::format("impl {} for {}: GAT param '{}' shadows impl type param",
                                              trait_name, target, gtp.name));
                // Bug 4 fix: impl GAT arity must match the trait's declaration.
                auto tit_gat = find_trait_iter_scoped(trait_name);
                if (tit_gat != traits_.end()) {
                    for (auto& at_def : tit_gat->second.assoc_types) {
                        if (at_def.name == aname && at_def.type_params.size() != gat_tps.size()) {
                            error(std::format(
                                "impl {} for {}: associated type '{}' has {} GAT params but trait declares {}",
                                trait_name, target, aname,
                                gat_tps.size(), at_def.type_params.size()));
                            break;
                        }
                    }
                }
                push_type_params(gat_tps);
                auto atype = resolve_type(map_of(m.get(la::TYPE.code)));
                pop_type_params(gat_tps);
                if (ast_elided_ref_(map_of(m.get(la::TYPE.code))))
                    error(std::format("impl {} for {}: associated type '{}' contains a borrowed value with an elided lifetime (E0106) — name it, e.g. `impl<'a> ... {{ type {} = &'a ... }}`",
                                      trait_name, target, aname, aname));
                AssocTypeEntry ate{atype, impl_tps, gat_tps, std::move(pending_doc_)};
                pending_doc_.clear();
                assoc_type_impls_[key] = ate;
                if (!cur_from_binary_) user_assoc_type_impl_keys_.insert(key);
                // Plain key: first-impl-wins; erase on a second distinct-args
                // impl so bare ambiguous lookups fail loud (G156-1). When
                // targ_sfx is empty (non-generic trait) key==pkey already —
                // nothing more to do.
                if (!targ_sfx.empty()) {
                    if (!assoc_type_impls_.count(pkey)) {
                        assoc_type_impls_[pkey] = std::move(ate);
                        if (!cur_from_binary_) user_assoc_type_impl_keys_.insert(pkey);
                    } else {
                        // A different-args impl already claimed the plain key →
                        // the bare projection is now ambiguous. Drop it.
                        assoc_type_impls_.erase(pkey);
                        user_assoc_type_impl_keys_.erase(pkey);
                    }
                }
            } else if (code_of(m) == la::ASSOC_CONST_IMPL) {
                auto cname = std::string(str_of(m.get(la::NAME.code)));
                std::string assoc_doc = std::move(pending_doc_);
                pending_doc_.clear();
                if (trait_name.empty()) {
                    // B97: inherent assoc-const on `impl S { const C: T = ...; }`
                    // is allowed; register it under "inherent::<target>::<name>".
                    TypeRef ctype = nullptr;
                    if (m.has_key(la::TYPE))
                        ctype = resolve_type(map_of(m.get(la::TYPE.code)));
                    std::string key = "inherent::" + target + "::" + cname;
                    assoc_const_impls_[key] = { ctype, m.get(la::VALUE), nullptr, std::move(assoc_doc) };
                    if (!cur_from_binary_) user_assoc_const_impl_keys_.insert(key);
                } else {
                    TypeRef ctype = nullptr;
                    if (m.has_key(la::TYPE))
                        ctype = resolve_type(map_of(m.get(la::TYPE.code)));
                    // Type check: impl's type must match the trait's declared type.
                    auto tit2 = find_trait_iter_scoped(trait_name);
                    if (tit2 != traits_.end() && ctype) {
                        for (auto& ac_def : tit2->second.assoc_consts) {
                            if (ac_def.name == cname && ac_def.type) {
                                if (!types_equal(ac_def.type, ctype))
                                    error(std::format(
                                        "impl {} for {}: associated constant '{}' declared as '{}' but trait requires '{}'",
                                        trait_name, target, cname,
                                        type_str(ctype), type_str(ac_def.type)));
                                break;
                            }
                        }
                    }
                    std::string key = trait_name + "::" + target + "::" + cname;
                    assoc_const_impls_[key] = { ctype, m.get(la::VALUE), nullptr, std::move(assoc_doc) };
                    if (!cur_from_binary_) user_assoc_const_impl_keys_.insert(key);
                }
            }
        }
        // Defensive: clear pending_doc_ at end of impl-body iteration so
        // it doesn't leak into the surrounding collect_module loop.
        pending_doc_.clear();
    }
    // If this is a blanket impl with no fn methods (only assoc-types), the
    // items loop didn't push anything to blanket_impls_. Push a marker
    // entry now so trait-satisfaction queries (sema_has_impl_recursive,
    // assoc_eqs_satisfied) can see it. method_name stays empty.
    if (is_blanket && blanket_impls_.size() == blanket_size_before) {
        BlanketImpl bi_rec;
        bi_rec.trait_name = trait_name;
        bi_rec.target_typevar = target;
        bi_rec.bound_trait = blanket_bound_trait;
        bi_rec.extra_bounds = blanket_extra_bounds;
        // B-mv-03: trait IDENTITIES alongside the raw spellings.
        bi_rec.canonical_trait = blanket_trait_canonical;
        bi_rec.canonical_bound_trait = blanket_bound_canonical;
        bi_rec.canonical_extra_bounds = blanket_extra_canonical;
        // method_name / mangled_name intentionally empty — this is a
        // satisfaction-only marker, not a method-dispatch entry.
        bi_rec.primary_assoc_eqs = blanket_primary_assoc_eqs;
        bi_rec.extra_assoc_eqs = blanket_extra_assoc_eqs;
        // M5 step 5c: for satisfaction markers (empty mangled), tag with
        // a synthetic "$marker$<trait>$<bound>$<target>" so the snapshot
        // filter can drop user-origin entries by mangled-name.
        if (!cur_from_binary_) {
            std::string marker = "$marker$" + trait_name + "$" +
                                 blanket_bound_trait + "$" + target;
            user_blanket_mangled_.insert(marker);
            bi_rec.mangled_name = std::move(marker);
        }
        blanket_impls_.push_back(std::move(bi_rec));
    }

    // Check completeness: every required trait method must be in the impl.
    // Default methods are registered as Target__method if not overridden.
    // Blanket impls use a synthetic target in their registrations; apply the
    // same mapping here so the completeness check sees the real methods.
    std::string check_target = is_blanket
        ? ("$blanket$" + trait_name + "$" + blanket_bound_trait + "$" + target)
        : target;
    if (!trait_name.empty()) {
        auto tit = find_trait_iter_scoped(trait_name);
        if (tit != traits_.end()) {
            for (auto& m : tit->second.methods) {
                auto mangled = check_target + "__" + m.name;
                // Trait-aware mangling: a method that collided with another
                // trait's same-named method was re-keyed under the
                // trait-qualified base; check that first.
                // G156-1: when this impl has concrete trait type-args, the method
                // was re-keyed under the args-aware base `<tgt>__<Trait>$<args>__<m>`
                // (so `impl Trait<u64>` and `impl Trait<u8>` coexist). Look that up
                // first, then the bare trait-qualified base, then the plain name.
                std::string targ_sfx = trait_targ_suffix(trait_type_args);
                auto cands = !targ_sfx.empty()
                    ? find_func_candidates(check_target + "__" + trait_name + targ_sfx + "__" + m.name)
                    : std::vector<const SemaFuncInfo*>{};
                if (cands.empty()) cands = find_func_candidates(
                    check_target + "__" + trait_name + "__" + m.name);
                if (cands.empty()) cands = find_func_candidates(mangled);
                // Find if THIS specific overload was explicitly provided.
                // Match by arity and non-receiver param types.
                // Receiver (param[0]) is &TypeVar(Self) in the trait vs
                // &ConcreteType in the impl — always skip it.
                // A TypeVar in the trait param (possibly inside Ref/Ptr) is a
                // generic param and matches any concrete impl type.
                auto is_generic_param = [](TypeRef t) -> bool {
                    while (t && (TypeRef(t).kind() == LogosType::Kind::Ref ||
                                 TypeRef(t).kind() == LogosType::Kind::MutRef ||
                                 TypeRef(t).kind() == LogosType::Kind::Ptr))
                        t = TypeRef(t).pointee();
                    if (!t) return false;
                    // TypeVar = generic type param (T); AssocType = T::Item
                    // Both are polymorphic from the trait's perspective and
                    // match any concrete type in the impl.
                    TypeRef tv{t};
                    return tv.kind() == LogosType::Kind::TypeVar ||
                           tv.kind() == LogosType::Kind::AssocType;
                };
                // Fn-family epic step B: detect a variadic trait type
                // param (`pub trait Fn<A...> { fn call(&self, args: A...)
                // ... }`). If the trait method uses the pack TypeVar at
                // some position, the impl is allowed to expose any number
                // of concrete params from that position onward — pack
                // absorbs them.
                //
                // Step B.4 (Deferred-3): per-element type check past the
                // pack position. The impl block carries `trait_type_args`
                // (e.g. `impl Fn<i32, i32> for Foo` → [i32, i32]); we
                // unify each post-pack impl-method param against the
                // corresponding `trait_type_args` element so an impl that
                // exposes `fn call(&self, x: u8, y: bool)` against
                // `Fn<i32, i32>` gets rejected instead of silently bound.
                std::string variadic_tp_name;
                for (auto& tp : tit->second.type_params) {
                    if (tp.is_variadic) { variadic_tp_name = tp.name; break; }
                }
                // S2b: substitute the impl's CONCRETE trait args into the
                // declared method signature before comparing. `fn f(&self,
                // cnt: &[u64; N])` in `trait T<const N: u32>` must compare as
                // `&[u64; 1]` against an `impl T<K, 1>` method — without the
                // substitution a const-param-sized array in a trait method
                // makes every impl method "missing" (the conformance check is
                // one more site of the array-length disease). Bound TYPE
                // params substitute too — the leftover-generic skips below
                // still cover the unbound ones.
                SemaSubst trait_arg_subst;
                {
                    auto& tps = tit->second.type_params;
                    for (size_t ti = 0; ti < tps.size() && ti < trait_type_args.size(); ++ti) {
                        if (trait_type_args[ti])
                            trait_arg_subst[tps[ti].name] = trait_type_args[ti];
                    }
                }
                // `Self` too: without it every `&Self` parameter is skipped
                // WHOLE by is_generic_param below and the impl may declare any
                // type in that slot. PROBES.md 2026-09-04y.
                if (impl_self_ty) trait_arg_subst["Self"] = impl_self_ty;
                std::string self_mismatch_note;
                const SemaFuncInfo* matching = nullptr;
                // PROBES.md 2026-09-05z: a candidate reached the signature compare.
                bool _sigdef_arity_seen = false;
                // PROBES.md 2026-09-06a: the trait declares exactly one method
                // of this name, so no other declaration can be the impl's real
                // subject. Without it a legal same-name overload is refused.
                bool _sigdef_name_unique = true;
                {
                    int _nsame = 0;
                    for (auto& _mm : tit->second.methods)
                        if (_mm.name == m.name) ++_nsame;
                    _sigdef_name_unique = (_nsame == 1);
                }
                for (auto* c : cands) {
                    int variadic_pos = -1;
                    if (!variadic_tp_name.empty()) {
                        for (size_t k = 1; k < m.param_types.size(); ++k) {
                            auto tp = m.param_types[k];
                            if (tp &&
                                TypeRef(tp).kind() == LogosType::Kind::TypeVar &&
                                TypeRef(tp).type_var_name() == variadic_tp_name) {
                                variadic_pos = (int)k;
                                break;
                            }
                        }
                    }
                    bool has_pack = (variadic_pos >= 0);
                    if (has_pack) {
                        if (c->param_types.size() < (size_t)variadic_pos) continue;
                    } else {
                        if (c->param_types.size() != m.param_types.size()) continue;
                    }
                    _sigdef_arity_seen = true;
                    bool sig_match = true;
                    // PROBE sigalphaw/sigalphas/sigalphapar/sigalpharet — M-SIG step 2. PROBES.md.
                    std::vector<std::pair<std::string,std::string>> _amap;
                    // LANDED 2026-09-07b (was PROBE sigsubs): the trait's slot 0
                    // is SUBSTITUTED before the alpha compare, and elided pairs
                    // only with elided. Control revert = `git revert`. PROBES.md.
                    const bool _asub = true;
                    bool _astrict = logos::probe::on("sigalphas") || _asub;
                    bool _apar = logos::probe::on("sigalphaw") ||
                                 logos::probe::on("sigalphas") ||
                                 logos::probe::on("sigalphapar") || _asub;
                    bool _aret = logos::probe::on("sigalphaw") ||
                                 logos::probe::on("sigalphas") ||
                                 logos::probe::on("sigalpharet") || _asub;
                    auto _acollect = [](TypeRef t, std::vector<std::string>& o,
                                        auto& self) -> void {
                        if (!t) return;
                        using K2 = LogosType::Kind;
                        switch (TypeRef(t).kind()) {
                            case K2::Ref: case K2::MutRef:
                                o.push_back(std::string(std::string_view(TypeRef(t).lifetime())));
                                self(TypeRef(t).pointee(), o, self); break;
                            case K2::Ptr:
                                self(TypeRef(t).pointee(), o, self); break;
                            case K2::Slice: case K2::Array:
                                o.push_back(std::string(std::string_view(TypeRef(t).lifetime())));
                                self(TypeRef(t).elem(), o, self); break;
                            case K2::TraitObject: case K2::DstRef:
                                o.push_back(std::string(std::string_view(TypeRef(t).lifetime())));
                                break;
                            case K2::Tuple:
                                for (auto e : TypeRef(t).tuple_elems()) self(e, o, self);
                                break;
                            case K2::FnPtr: case K2::Closure:
                                for (auto p : TypeRef(t).closure_params()) self(p, o, self);
                                self(TypeRef(t).closure_ret(), o, self); break;
                            case K2::AssocType:
                                self(TypeRef(t).assoc_base(), o, self);
                                for (auto g : TypeRef(t).gat_args()) self(g, o, self);
                                for (auto& l : TypeRef(t).lifetime_args()) o.push_back(l);
                                break;
                            default:
                                for (auto a : TypeRef(t).type_args()) self(a, o, self);
                                for (auto& l : TypeRef(t).lifetime_args()) o.push_back(l);
                                break;
                        }
                    };
                    // Rust ELISION, expanded per signature before the alpha
                    // compare: fresh binder per elided input, an elided output
                    // takes the receiver's. LANDED 2026-09-09elide. PROBES.md.
                    auto _esyn = [](size_t slot, size_t idx) -> std::string {
                        return std::format("#e{}_{}", slot, idx);
                    };
                    // Filled once `check_end` is known; "" = not expandable.
                    std::string _eout_a, _eout_b;
                    auto _eout_of = [&](const std::vector<TypeRef>& ps,
                                        size_t upto) -> std::string {
                        using K5 = LogosType::Kind;
                        if (m.has_self_receiver && !ps.empty() && ps[0] &&
                            (TypeRef(ps[0]).kind() == K5::Ref ||
                             TypeRef(ps[0]).kind() == K5::MutRef)) {
                            std::vector<std::string> ls;
                            _acollect(ps[0], ls, _acollect);
                            if (!ls.empty())
                                return ls[0].empty() ? _esyn(0, 0) : ls[0];
                        }
                        std::string only; size_t n = 0;
                        for (size_t k = 0; k < upto && k < ps.size(); ++k) {
                            std::vector<std::string> ls;
                            _acollect(ps[k], ls, _acollect);
                            for (size_t i = 0; i < ls.size(); ++i) {
                                ++n;
                                only = ls[i].empty() ? _esyn(k, i) : ls[i];
                            }
                        }
                        return n == 1 ? only : std::string();
                    };
                    auto _alpha_ok = [&](TypeRef ta, TypeRef tb, size_t slot,
                                         bool is_ret) -> bool {
                        std::vector<std::string> la, lb;
                        _acollect(ta, la, _acollect);
                        _acollect(tb, lb, _acollect);
                        if (la.size() != lb.size()) return false;
                        for (size_t i = 0; i < la.size(); ++i) {
                            std::string x = la[i];
                            std::string y = lb[i];
                            if (x.empty()) x = is_ret ? _eout_a : _esyn(slot, i);
                            if (y.empty()) y = is_ret ? _eout_b : _esyn(slot, i);
                            if (x.empty() || y.empty()) {
                                // Elision not expandable here — the old rule stands.
                                if (_astrict && !(x.empty() && y.empty())) return false;
                                continue;
                            }
                            if (x == "static" || y == "static") {
                                if (x != y) return false;
                                continue;
                            }
                            bool bound = false;
                            for (auto& pr : _amap) {
                                if (pr.first == x) { if (pr.second != y) return false; bound = true; }
                                else if (pr.second == y) { return false; }
                            }
                            if (!bound) _amap.emplace_back(x, y);
                        }
                        return true;
                    };
                    // A DST-alias `Self` (`str` resolves to `[u8]` here while the
                    // impl's WRITTEN `str` resolves to `&[u8]`) makes the two
                    // sides differ by a reference layer at WHATEVER slot it
                    // appears in — receiver, parameter or return. A
                    // representation difference, never a lifetime fact: the
                    // alpha check declines to speak. PROBES.md 2026-09-07b.
                    auto _self_shape_artefact = [&](TypeRef raw, TypeRef sub,
                                                    TypeRef impl_t) -> bool {
                        using K3 = LogosType::Kind;
                        if (!raw || !sub || !impl_t) return false;
                        TypeRef bare = raw;
                        while (bare && (TypeRef(bare).kind() == K3::Ref ||
                                        TypeRef(bare).kind() == K3::MutRef))
                            bare = TypeRef(bare).pointee();
                        if (!bare || TypeRef(bare).kind() != K3::TypeVar ||
                            std::string_view(TypeRef(bare).type_var_name()) != "Self")
                            return false;
                        // No `Self` to substitute (a non-nominal impl target the
                        // chain above cannot rebuild) — the trait slot is a
                        // TypeVar the impl slot can never equal, and a comparator
                        // that speaks here states a FALSEHOOD. It declines.
                        if (!impl_self_ty) return true;   // nothing to substitute
                        // The alpha comparator's subject is LIFETIMES. When the
                        // substituted `Self` and the impl's written slot are not
                        // even the same type modulo lifetimes, the substitution
                        // and the impl's spelling disagree about the SHAPE of
                        // Self (`str` -> `[u8]` vs the written `&str` -> `&[u8]`;
                        // `&mut Self` -> `&mut i64` vs the written `&mut &mut i64`)
                        // and every sentence this check could print is false. The
                        // type compare owns that verdict; this one declines.
                        // DOOR 1 of `trait.impl.method-receiver-conforms`: the
                        // hatch covers a SAME-PREFIX representation difference
                        // only. PROBES.md 2026-09-10a-selfrecvland.
                        {
                            auto _pfx = [](TypeRef t) {
                                std::string s;
                                while (t && (TypeRef(t).kind() == K3::Ref ||
                                             TypeRef(t).kind() == K3::MutRef)) {
                                    s += (TypeRef(t).kind() == K3::MutRef) ? 'm' : 'r';
                                    t = TypeRef(t).pointee();
                                }
                                return s;
                            };
                            if (_pfx(sub) != _pfx(impl_t)) return false;
                        }
                        return !types_equal(sub, impl_t);
                    };
                    size_t check_end = has_pack
                        ? (size_t)variadic_pos
                        : m.param_types.size();
                    // The elided-output binder per side, over the compared
                    // INPUT slots, the trait's substituted first. PROBES.md.
                    {
                        std::vector<TypeRef> _etr(m.param_types);
                        if (!trait_arg_subst.empty())
                            for (auto& _t : _etr)
                                if (_t) _t = subst_type_sema(_t, trait_arg_subst);
                        _eout_a = _eout_of(_etr, check_end);
                        _eout_b = _eout_of(c->param_types, check_end);
                    }
                    // PROBE sigselflt — M-SIG(a), the k=0 slot the loop below
                    // skips. DECLINED 2026-09-02w: it refuses a legal
                    // ALPHA-RENAMING of a method binder. See PROBES.md.
                    if (logos::probe::on("sigselflt") && sig_match &&
                        !m.param_types.empty() && !c->param_types.empty() &&
                        m.param_types[0] && c->param_types[0]) {
                        TypeRef ts = m.param_types[0], cs = c->param_types[0];
                        using K = LogosType::Kind;
                        if ((TypeRef(ts).kind() == K::Ref ||
                             TypeRef(ts).kind() == K::MutRef) &&
                            TypeRef(cs).kind() == TypeRef(ts).kind() &&
                            std::string_view(TypeRef(ts).lifetime()) !=
                            std::string_view(TypeRef(cs).lifetime()) &&
                            !std::string_view(TypeRef(ts).lifetime()).empty())
                            sig_match = false;
                        if (sig_match &&
                            (TypeRef(ts).kind() == K::Ref ||
                             TypeRef(ts).kind() == K::MutRef) &&
                            TypeRef(cs).kind() == TypeRef(ts).kind() &&
                            std::string_view(TypeRef(ts).lifetime()).empty() &&
                            !std::string_view(TypeRef(cs).lifetime()).empty())
                            sig_match = false;
                    }
                    if (_apar && sig_match && !m.param_types.empty() &&
                        !c->param_types.empty() && m.param_types[0] &&
                        c->param_types[0]) {
                        TypeRef _t0 = m.param_types[0];
                        bool _t0_collapsed = false;
                        if (_asub && !trait_arg_subst.empty()) {
                            _t0 = subst_type_sema(m.param_types[0], trait_arg_subst);
                            _t0_collapsed = _self_shape_artefact(
                                m.param_types[0], _t0, c->param_types[0]);
                        }
                        // DOOR 2 of `trait.impl.method-receiver-conforms`, in
                        // SERIES with door 1. PROBES.md 2026-09-10a-selfrecvland.
                        if (!_t0_collapsed &&
                            (!_alpha_ok(_t0, c->param_types[0], 0, false) ||
                             !types_equal(_t0, c->param_types[0]))) {
                            if (_asub && self_mismatch_note.empty())
                                self_mismatch_note = std::format(
                                    "the receiver is declared '{}' and the impl "
                                    "declares '{}'", type_str(_t0),
                                    type_str(c->param_types[0]));
                            sig_match = false;
                        }
                    }
                    for (size_t k = 1; sig_match && k < check_end; ++k) {
                        auto tp = m.param_types[k];
                        auto cp = c->param_types[k];
                        if (!tp || !cp) { sig_match = false; break; }
                        if (!trait_arg_subst.empty())
                            tp = subst_type_sema(tp, trait_arg_subst);
                        // If the trait param is generic (TypeVar or AssocType),
                        // it matches any concrete impl type.
                        if (is_generic_param(tp)) continue;
                        if (is_generic_param(cp)) continue;
                        // PROBE sigparamlt — M-SIG(c), DECLINED 2026-09-02w:
                        // it refuses a legal ALPHA-RENAMING. See PROBES.md.
                        if (logos::probe::on("sigparamlt") &&
                            !detail::types_equal_with_lifetimes(tp, cp))
                            { sig_match = false; break; }
                        if (!types_equal(tp, cp)) {
                            // Only reachable for a `Self`-shaped trait slot
                            // because of the substitution above.
                            if (self_mismatch_note.empty() &&
                                is_generic_param(m.param_types[k]))
                                self_mismatch_note = std::format(
                                    "parameter {} is declared '{}' and the impl "
                                    "declares '{}'", k, type_str(tp), type_str(cp));
                            sig_match = false; break;
                        }
                        if (_apar && !(_asub && _self_shape_artefact(m.param_types[k], tp, cp))
                            && !_alpha_ok(tp, cp, k, false)) {
                            if (_asub && self_mismatch_note.empty())
                                self_mismatch_note = std::format(
                                    "parameter {} is declared '{}' and the impl "
                                    "declares '{}'", k, type_str(tp), type_str(cp));
                            sig_match = false; break;
                        }
                    }
                    // Per-element check past the pack position: each
                    // impl-method param at index k (where k >=
                    // variadic_pos) corresponds to trait_type_args[k -
                    // variadic_pos]. Mismatch rejects the candidate.
                    if (has_pack && sig_match) {
                        for (size_t k = (size_t)variadic_pos;
                             k < c->param_types.size(); ++k) {
                            size_t targ_idx = k - (size_t)variadic_pos;
                            if (targ_idx >= trait_type_args.size()) {
                                // Impl exposes more args than the pack
                                // instantiation has — concrete arity
                                // overflow.
                                sig_match = false; break;
                            }
                            auto exp = trait_type_args[targ_idx];
                            auto cp  = c->param_types[k];
                            if (!exp || !cp) continue;
                            if (is_generic_param(exp)) continue;
                            if (is_generic_param(cp)) continue;
                            if (!types_equal(exp, cp)) {
                                sig_match = false; break;
                            }
                        }
                        // Trait pack carries more args than impl exposes —
                        // arity underflow (less common; the
                        // `c->param_types.size() < variadic_pos` guard
                        // catches the leading-args case but not the
                        // pack-itself-arity).
                        if (sig_match &&
                            (c->param_types.size() - (size_t)variadic_pos)
                                != trait_type_args.size())
                            sig_match = false;
                    }
                    // PROBE sigretlt — M-SIG(b), the return type nothing
                    // compares. DECLINED 2026-09-02w at cost 1099. See PROBES.md.
                    if (sig_match && logos::probe::on("sigretlt") &&
                        m.ret_type && c->ret_type) {
                        TypeRef tr = m.ret_type;
                        if (!trait_arg_subst.empty())
                            tr = subst_type_sema(tr, trait_arg_subst);
                        if (!is_generic_param(tr) &&
                            !is_generic_param(c->ret_type) &&
                            !detail::types_equal_with_lifetimes(tr, c->ret_type))
                            sig_match = false;
                    }
                    // LANDED 2026-09-09sigland: the return slot is compared BY
                    // TYPE, not only by `_alpha_ok`'s lifetime strings. PROBES.md.
                    if (sig_match && _aret && m.ret_type && c->ret_type) {
                        TypeRef tra = m.ret_type;
                        if (!trait_arg_subst.empty())
                            tra = subst_type_sema(tra, trait_arg_subst);
                        if (!is_generic_param(tra) &&
                            !is_generic_param(c->ret_type) &&
                            !(_asub && _self_shape_artefact(m.ret_type, tra, c->ret_type)) &&
                            (!_alpha_ok(tra, c->ret_type, check_end, true) ||
                             !types_equal(tra, c->ret_type))) {
                            if (_asub && self_mismatch_note.empty())
                                self_mismatch_note = std::format(
                                    "the return type is declared '{}' and the "
                                    "impl declares '{}'", type_str(tra),
                                    type_str(c->ret_type));
                            sig_match = false;
                        }
                    }
                    if (sig_match) { matching = c; break; }
                }
                if (matching) {
                    if (early_bound_lts_(m.lifetime_params, m.lifetime_outlives, m.type_params) !=
                        early_bound_lts_(matching->lifetime_params, matching->lifetime_outlives, matching->type_params))
                        error(std::format("impl {} for {}: lifetime parameters or bounds on method '{}' do not match the trait declaration (E0195)",
                                          trait_name, target, m.name));
                    if (m.is_unsafe != matching->is_unsafe) {
                        error(std::format("impl {} for {}: method '{}' has mismatched unsafe parity (trait: {}, impl: {})",
                            trait_name, target, m.name,
                            m.is_unsafe ? "unsafe" : "safe",
                            matching->is_unsafe ? "unsafe" : "safe"));
                    }
                } else if (m.has_default && _sigdef_arity_seen &&
                           _sigdef_name_unique) {
                    // A default body does NOT exempt an override from the
                    // conformance check. PROBES.md 2026-09-06a.
                    if (!self_mismatch_note.empty())
                        error(std::format("impl {} for {}: method '{}' does not match "
                              "the trait declaration: {}",
                              trait_name, target, m.name, self_mismatch_note));
                    else
                        error(std::format("impl {} for {}: method '{}' does not match the trait declaration's signature",
                              trait_name, target, m.name));
                } else if (m.has_default) {
                    // This overload not explicitly provided; register the default.
                    // Build Self type; for generic impls include the type params as TypeVars.
                    TypeRef self_type = nullptr;
                    {
                        auto [spkg_def, ssi_def] = find_struct_by_name(target);
                        auto [dpkg_def, dsi_def] = find_datatype_by_name(target);
                        auto _shaped_target = [](TypeRef pat) -> bool {
                            if (!pat) return false;
                            for (auto a : TypeRef(pat).type_args()) {
                                if (!a) continue;
                                auto k = TypeRef(a).kind();
                                if (k != LogosType::Kind::TypeVar &&
                                    k != LogosType::Kind::ConstVar)
                                    return true;
                            }
                            return false;
                        };
                        if (ssi_def) {
                            // Shaped target → Self = the impl's pattern (see
                            // sema_decl synthesis; both sides must agree or
                            // declared vs body types of defaults diverge).
                            if (target_resolved && _shaped_target(target_resolved)) {
                                self_type = target_resolved;
                            } else if (!impl_tps.empty()) {
                                std::vector<TypeRef> tv_args;
                                for (auto& tp : impl_tps)
                                    tv_args.push_back(make_typevar(tp.name));
                                self_type = make_generic_struct(
                                    target, std::move(tv_args),
                                    self_lt_args_(ssi_def->lifetime_params),
                                    spkg_def);
                            } else if (!ssi_def->lifetime_params.empty()) {
                                // The SAME defect as at the impl header above,
                                // in the synthesis of an inherited default.
                                logos::probe::census("self.lts-restored.default");
                                self_type = make_generic_struct(target, {},
                                                                self_lt_args_(ssi_def->lifetime_params),
                                                                spkg_def);
                            } else {
                                self_type = make_struct_type(target, spkg_def);
                            }
                        } else if (dsi_def) {
                            if (target_resolved && _shaped_target(target_resolved)) {
                                self_type = target_resolved;
                            } else if (!impl_tps.empty()) {
                                std::vector<TypeRef> tv_args;
                                for (auto& tp : impl_tps)
                                    tv_args.push_back(make_typevar(tp.name));
                                self_type = make_generic_datatype(target, std::move(tv_args), {}, dpkg_def);
                            } else {
                                self_type = make_datatype_type(target, dpkg_def);
                            }
                        } else if (auto prim_t = lookup_type_by_name(target);
                                   prim_t && [&]{
                                       auto k = TypeRef(prim_t).kind();
                                       using K = LogosType::Kind;
                                       return k==K::I8||k==K::I16||k==K::I24||k==K::I32||
                                              k==K::I56||k==K::I64||k==K::I128||
                                              k==K::U8||k==K::U16||k==K::U24||k==K::U32||
                                              k==K::U56||k==K::U64||k==K::U128||
                                              k==K::Usize||k==K::Isize||
                                              k==K::F32||k==K::F64||k==K::Bool||k==K::Char;
                                   }()) {
                            // G160-3: `impl Trait for <SCALAR primitive>` (i64/
                            // u32/bool/char/…). Bind Self to the primitive so a
                            // default body's `&Self` signature resolves. Without
                            // this, every inherited default beyond the first
                            // (which only worked by inheriting a leaked Self)
                            // failed with "unknown type 'Self'". RESTRICTED to
                            // scalar kinds — `str` (a slice alias) / enum targets
                            // (Option/Result) must NOT be bound here (their
                            // defaults rely on Self staying a TypeVar for generic
                            // eq inference).
                            self_type = prim_t;
                        }
                    }
                    // Blanket impl `impl<T: Bound> Trait for T {}`: the default
                    // method must be synthesized as a generic fn under the
                    // synthetic `$blanket$...` target (so dispatch can find it)
                    // with Self = the blanket TypeVar, and a blanket_impls_
                    // entry pushed so try_blanket_method_dispatch surfaces it on
                    // any concrete receiver satisfying Bound. Without this, the
                    // trait's defaults are invisible on a blanket impl.
                    std::string def_reg_target = is_blanket ? check_target : target;
                    if (is_blanket)
---
            self_type = target_resolved;
        // PROBE implanon / selfv / selfvg — src/compiler/PROBES.md 2026-09-13a.
        if (self_type && (logos::probe::on("implanon") || logos::probe::on("selfv") || logos::probe::on("selfvg") || logos::probe::on("implanonx") || logos::probe::on("selfvee") || logos::probe::on("selfveu"))) {
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
    }
    // Verify trait exists (only for trait impls)
    // Copy and Drop are built-in marker traits — not always visible through
    // the dependency-graph (pub trait + use isn't enough when the target
    // type's own package re-imports a different non-pub Drop, e.g. std.string
    // and writ.zone both used to declare local `trait Drop`). Treating Drop
    // as a built-in matches Copy and lets the impl resolve via name alone.
    const bool builtin_marker_ = !trait_name.empty() &&
        !(trait_name != "Copy" && trait_name != "Drop");
    const bool trait_is_drop_ = builtin_marker_ && trait_name != "Copy";
    if (!trait_name.empty() && !builtin_marker_ && !traits_.count(trait_name))
        error(std::format("impl: unknown trait '{}'", trait_name));
    // rustc check_drop_impl: E0120 / E0366 / E0367 at the declaration. PROBES.md 2026-09-02u.
    if (trait_is_drop_) check_drop_impl_wf(target, target_resolved, impl_tps, node);
    // Phase 6: scope the impl's trait name so `Self::Item<X>` inside
    // method bodies / signatures resolves before impls_ is populated.
    current_impl_trait_name_ = trait_name;
    // Resolve the trait's PACKAGE once, through the reader that already
    // implements Rust's shadowing order (cur_package_::Name first). A user
    // `trait Drop` in this package resolves to THIS package; the prelude's
    // resolves to logos.lang.drop. Empty when the name resolves to nothing
    // (builtin_marker_ lets `Drop`/`Copy` impls through with no declaration).
    current_impl_trait_package_.clear();
    if (!trait_name.empty()) {
        auto tit_ = find_trait_iter_scoped(trait_name);
        if (tit_ != traits_.end()) current_impl_trait_package_ = tit_->second.package;
    }
    // Resolve trait type args (e.g. impl Into<i32> for Celsius → T=i32)
    // and push them into current_type_params_ so method sigs resolve correctly.
    std::vector<TypeRef> trait_type_args;
    // B62: parallel collection of lifetime args at trait position
    // (`impl Trait<'a, T>` → ["a"]). Skipped from type_args resolution but
    // captured for HRTB satisfaction check at bound time.
    std::vector<std::string> trait_lt_args;
    if (!trait_name.empty() && node.has_key(la::TYPE_PARAMS)) {
        AnyVal tpav = node.get(la::TYPE_PARAMS.code);
        if (!tpav.is_null()) {
            auto tplist = map_of(tpav);
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i) {
                    auto item = map_of(items.get(i));
                    // L1: skip LIFETIME_PARAM entries — they're lifetime
                    // arg position in `impl Foo<'a> for ...`. Logos doesn't
                    // track regions structurally for trait dispatch, and
                    // resolve_type would error on code 131 (LIFETIME_PARAM).
                    if (code_of(item) == la::LIFETIME_PARAM) {
                        trait_lt_args.push_back(
                            std::string(str_of(item.get(la::NAME.code))));
                        continue;
                    }
                    trait_type_args.push_back(resolve_type(item));
                }
            }
        }
        auto tit = find_trait_iter_scoped(trait_name);
        if (tit != traits_.end()) {
            for (size_t i = 0; i < tit->second.type_params.size() &&
                                i < trait_type_args.size(); ++i)
                current_type_params_[tit->second.type_params[i].name] = trait_type_args[i];
        }
    }
    // G156-1: expose this impl's concrete trait type-args to collect_fn so the
    // method collision-detection can mangle by them (empty for inherent impls).
    current_impl_trait_args_ = trait_type_args;
    // Detect blanket impl: `impl<T: Bound> Trait for T` — target IS one of
    // this impl's own type parameters.  Methods are collected as generic fns
    // (target becomes the TypeVar name, e.g. "T__method"); later, at call
    // sites on concrete types satisfying Bound, we instantiate the blanket.
    bool is_blanket = false;
    std::string blanket_bound_trait;
    std::vector<std::string> blanket_extra_bounds;
    // B-mv-03: identities parallel to the two above (see BlanketImpl).
    // `blanket_trait_canonical` is the identity of the trait BEING implemented;
    // resolved here rather than reusing `coh_trait` (computed at the bottom of
    // this function) only because the pushes below happen first. Same call, same
    // scope, so the two agree by construction — `check_impl_registry_key_identity`
    // is what makes "by construction" mechanical rather than asserted.
    std::string blanket_trait_canonical =
        trait_name.empty() ? std::string() : canonical_trait_name(trait_name);
    std::string blanket_bound_canonical;
    std::vector<std::string> blanket_extra_canonical;
    // ADR 0008: associated-type equality clauses parallel to the bound list.
    std::vector<std::pair<std::string, TypeRef>> blanket_primary_assoc_eqs;
    std::vector<std::pair<std::string,
        std::vector<std::pair<std::string, TypeRef>>>> blanket_extra_assoc_eqs;
    if (!trait_name.empty()) {
        for (auto& tp : impl_tps) {
            if (tp.name == target) {
                is_blanket = true;
                if (!tp.bounds.empty()) {
                    blanket_bound_trait = tp.bounds[0].trait_name;
                    // B-mv-03: the bound's IDENTITY, already captured on the
                    // TraitBound at read time (read_trait_bound_args) in THIS
                    // impl's declaring scope. THIS is the string the blanket
                    // walk asks the registry with — `impl<T: Hash> Marker for T`
                    // written next to a package-local `trait Hash` must admit
                    // that Hash's concretes and not the stdlib Hash's.
                    blanket_bound_canonical = tp.bounds[0].canonical_trait;
                    blanket_primary_assoc_eqs = tp.bounds[0].assoc_eqs;
                    for (size_t bi = 1; bi < tp.bounds.size(); ++bi) {
                        blanket_extra_bounds.push_back(tp.bounds[bi].trait_name);
                        blanket_extra_canonical.push_back(tp.bounds[bi].canonical_trait);
                        blanket_extra_assoc_eqs.emplace_back(
                            tp.bounds[bi].trait_name, tp.bounds[bi].assoc_eqs);
                    }
                }
                break;
            }
        }
    }

    // Snapshot blanket_impls_ size so we can detect after the items loop
    // whether *any* per-method blanket entry got pushed. Blankets that
    // declare only assoc-types (no fn methods) need a marker entry so
    // trait-satisfaction queries can find them — without it,
    // sema_has_impl_recursive would report the blanket trait as
    // unsatisfied for any concrete that depends on it.
    size_t blanket_size_before = blanket_impls_.size();

    // Register impl methods as free functions with mangled names: Target__method
    // Also collect associated type definitions.
    // Skip if already registered (e.g. class methods defined inline).
    if (node.has_key(la::ITEMS)) {
        auto items = arr_of(node.get(la::ITEMS.code));
        // Phase A.2: sweep doc-lines into pending_doc_ so the next
        // collect_fn invocation picks them up via take_pending_doc().
        pending_doc_.clear();
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto m = map_of(items.get(i));
            if (try_append_doc(pending_doc_, m)) continue;
            if (code_of(m) == la::REL_OP) {
                // ADR 0024 S6: `op entry.key eq = __ctr_at_Hs…  exact;` — ONE
                // access operation the source publishes. Collected onto the
                // rel binding it names, so a planner reads what the source
                // SAYS rather than guessing from a materializer's name.
                std::string lead(str_of(m.get(la::REL_KW.code)));
                if (lead != "op") {
                    error(std::format(
                        "impl for '{}': unexpected member '{} …' — an access "
                        "operation is written `op <rel>.<col> <cmp> = <fn> "
                        "[exact];`", target, lead));
                    continue;
                }
                SourceRelOp so;
                std::string rn(str_of(m.get(la::TYPE_NAME.code)));
                so.col = std::string(str_of(m.get(la::FIELD.code)));
                so.cmp = std::string(str_of(m.get(la::OP.code)));
                // NAME in both forms: a `#ident` antiquotation is resolved INTO
                // NAME before this ever collects.
                so.fn  = std::string(str_of(m.get(la::NAME.code)));
                std::string flag(m.has_key(la::RET_TYPE)
                                 ? std::string(str_of(m.get(la::RET_TYPE.code)))
                                 : std::string());
                if (!flag.empty() && flag != "exact") {
                    error(std::format(
                        "impl for '{}': `op {}.{}` — unknown flag '{}'; the only "
                        "one is `exact` (absent = the operation returns a "
                        "SUPERSET and the query keeps its filter)",
                        target, rn, so.col, flag));
                    continue;
                }
                so.exact = (flag == "exact");
                if (so.cmp != "eq" && so.cmp != "ge" && so.cmp != "le"
                    && so.cmp != "gt" && so.cmp != "lt") {
                    error(std::format(
                        "impl for '{}': `op {}.{}` answers comparison '{}' — "
                        "expected one of eq / ge / le / gt / lt",
                        target, rn, so.col, so.cmp));
                    continue;
                }
                bool bound_found = false;
                for (auto& e : source_impls_[target])
                    if (e.rel == rn) {
                        bool dup = false;
                        for (auto& prev : e.ops)
                            if (prev.col == so.col && prev.cmp == so.cmp) {
                                // Collect runs in several phases, so seeing the
                                // same operation again is CONFIRMATION — the
                                // same rule rel_bind already follows. Only a
                                // conflicting redeclaration is an error.
                                if (prev.fn != so.fn || prev.exact != so.exact)
                                    error(std::format(
                                        "impl for '{}': `op {}.{} {}` declared "
                                        "twice with different meanings ('{}'{} "
                                        "vs '{}'{}) — one operation per "
                                        "(column, comparison)",
                                        target, rn, so.col, so.cmp,
                                        prev.fn, prev.exact ? " exact" : "",
                                        so.fn, so.exact ? " exact" : ""));
                                dup = true;
                                break;
                            }
                        if (!dup) e.ops.push_back(std::move(so));
                        bound_found = true;
                        break;
                    }
                if (!bound_found)
                    error(std::format(
                        "impl for '{}': `op {}.{}` names no bound rel — declare "
                        "`rel {} = <materializer>;` in this impl first",
                        target, rn, so.col, rn));
                continue;
            }
            if (code_of(m) == la::REL_BIND) {
                // ADR 0016 §6: `rel edge = writ_graph_edges;` — bind one
                // trait rel to its native materializer for this type.
                std::string lead(str_of(m.get(la::REL_KW.code)));
                std::string rn(str_of(m.get(la::NAME.code)));
                if (lead == "size") {
                    // ADR 0024 S4 — `size <rel> = <fn>;`. The source's row
                    // count, reported at RUN time by a fn of the source alone.
                    // Shares rel_bind's SHAPE (`<lead> <name> = <fn>;`), which
                    // is what REL_KW is for: the lead ident is data, and the
                    // member is told apart by reading it rather than by a rule
                    // of its own. ⚠ The AST→source renderer had "rel " baked in
                    // for this node and now prints REL_KW — under `-g` a dump is
                    // REPARSED, so a lead the renderer cannot say becomes a
                    // duplicate rel binding.
                    std::string szfn(str_of(m.get(la::VALUE.code)));
                    bool rel_found = false;
                    for (auto& e : source_impls_[target])
                        if (e.rel == rn) {
                            // Collect runs in several phases: the same
                            // declaration seen again is confirmation, and only a
                            // conflicting one is an error — the rule `op` and
                            // `rel` already follow.
                            if (e.size_fn.empty()) e.size_fn = szfn;
                            else if (e.size_fn != szfn)
                                error(std::format(
                                    "impl for '{}': `size {}` declared twice with "
                                    "different reporters ('{}' vs '{}') — a "
                                    "relation has one size",
                                    target, rn, e.size_fn, szfn));
                            rel_found = true;
                            break;
                        }
                    if (!rel_found)
                        error(std::format(
                            "impl for '{}': `size {}` names no bound rel — "
                            "declare `rel {} = <materializer>;` in this impl "
                            "first", target, rn, rn));
                    continue;
                }
                if (lead == "order") {
                    // ADR 0025 S3 — `order <rel> = <col>;`. WHICH column the
                    // relation's rows already arrive sorted by. Shares
                    // rel_bind's shape for the same reason `size` does: the
                    // lead ident is data, so a third member needs no grammar
                    // rule and gets none.
                    //
                    // ⚠ ONLY THE COLUMN NAME IS CHECKED HERE, and the other
                    // half of the pairing is checked at spec time on purpose.
                    // "Is this column one the relation declares?" is answerable
                    // from `sig->cols`, which this pass already holds. "Is the
                    // producer's return type actually `OrderedBy`?" is a
                    // question about a fn's return type and the impl graph, and
                    // collect runs in several phases over a partially populated
                    // one — asking it here would answer `false` for a working
                    // impl collected in a later phase, which is a REFUSAL that
                    // fires on correct code. `native_source_spec` asks it where
                    // the answer is stable.
                    std::string ocol(str_of(m.get(la::VALUE.code)));
                    bool rel_found = false;
                    for (auto& e : source_impls_[target])
                        if (e.rel == rn) {
                            bool known = false;
                            for (const auto& c : e.cols)
                                if (c.name == ocol) { known = true; break; }
                            if (!known) {
                                std::string have;
                                for (const auto& c : e.cols) {
                                    if (!have.empty()) have += ", ";
                                    have += c.name;
                                }
                                error(std::format(
                                    "impl for '{}': `order {} = {}` names no "
                                    "column of rel '{}' — its columns are ({}). "
                                    "The ordered column must be one the "
                                    "relation publishes, or no query could name "
                                    "it in an `order by`",
                                    target, rn, ocol, rn, have));
                                rel_found = true;
                                break;
                            }
                            // Collect runs in several phases: the same
                            // declaration seen again is confirmation, and only
                            // a conflicting one is an error — the rule `op`,
                            // `rel` and `size` already follow.
                            if (e.ord_col.empty()) e.ord_col = ocol;
                            else if (e.ord_col != ocol)
                                error(std::format(
                                    "impl for '{}': `order {}` declared twice "
                                    "over different columns ('{}' vs '{}') — a "
                                    "relation arrives in ONE order",
                                    target, rn, e.ord_col, ocol));
                            rel_found = true;
                            break;
                        }
                    if (!rel_found)
                        error(std::format(
                            "impl for '{}': `order {}` names no bound rel — "
                            "declare `rel {} = <materializer>;` in this impl "
                            "first", target, rn, rn));
                    continue;
                }
                if (lead != "rel") {
                    error(std::format(
                        "impl for '{}': unexpected member '{} {} = …' — the "
                        "leads of this shape are `rel <r> = <materializer>;`, "
                        "`size <r> = <reporter>;` and `order <r> = <col>;`",
                        target, lead, rn));
                    continue;
                }
                if (trait_name.empty()) {
                    error(std::format(
                        "impl for '{}': `rel {} = …` outside a trait impl — "
                        "rel bindings implement a source trait's vocabulary "
                        "(`impl GraphSource for {} {{ … }}`)",
                        target, rn, target));
                    continue;
                }
                auto trit = trait_rels_.find(trait_name);
                const TraitRelSig* sig = nullptr;
                if (trit != trait_rels_.end())
                    for (const auto& ts : trit->second)
                        if (ts.rel == rn) { sig = &ts; break; }
                if (!sig) {
                    error(std::format(
                        "impl {} for {}: trait '{}' declares no rel '{}'",
                        trait_name, target, trait_name, rn));
                    continue;
                }
                SourceRelBind b;
                b.trait_name = trait_name;
                b.rel    = rn;
                b.mat_fn = std::string(str_of(m.get(la::VALUE.code)));
                b.mat_module = cur_package_;   // refined at spec time if needed
                b.cols   = sig->cols;
                bool dup = false, same = false;
                for (const auto& e : source_impls_[target])
                    if (e.rel == rn) {
                        dup = true;
                        // The convention pre-scan (#[derive_graph_source])
                        // seeds this exact binding a round before the derive's
                        // emitted impl collects — identical re-registration
                        // is confirmation, not conflict.
                        same = (e.trait_name == trait_name && e.mat_fn == b.mat_fn);
                        break;
                    }
                if (dup) {
                    if (!same)
                        error(std::format(
                            "impl {} for {}: duplicate rel binding '{}'",
                            trait_name, target, rn));
                    continue;
                }
                source_impls_[target].push_back(std::move(b));
                continue;
            }
            if (code_of(m) == la::FN || code_of(m) == la::STATIC_FN) {
                auto mname = std::string(str_of(m.get(la::NAME.code)));
                // Blanket impls use a synthetic target name so the method
                // doesn't collide with `T::method` lookups on other generic
                // `T: Trait` type parameters that share the same letter.
                // Blanket key includes the bound trait so distinct blankets
                // on the same trait (e.g. `impl<DT: Primitive> T for DT` and
                // `impl<DT: PodRef> T for DT`) register under separate keys.
                std::string reg_target = is_blanket
                    ? ("$blanket$" + trait_name + "$" + blanket_bound_trait + "$" + target)
                    : target;
                auto mangled = reg_target + "__" + mname;
                collect_fn(m, reg_target, trait_name);
                // The written `self:` type of an INHERENT impl method,
                // checked against the impl target for the first time here —
                // this is the only site that has both. (`lower_fn`'s `Self` is
                // a bare name with no arguments.) PROBES.md 2026-09-04a.
                if (trait_name.empty() && impl_self_ty && m.has_key(la::PARAMS)) {
                    auto pav = m.get(la::PARAMS.code);
                    if (pav.is_pointer()) {
                        auto pnode = map_of(pav);
                        if (pnode.has_key(la::ITEMS)) {
                            auto pitems = arr_of(pnode.get(la::ITEMS.code));
                            if (pitems.size() > 0) {
                                auto p0 = map_of(pitems.get(0));
                                if (code_of(p0) == la::PARAM && p0.has_key(la::NAME) &&
                                    str_of(p0.get(la::NAME.code)) == "self" &&
                                    p0.has_key(la::TYPE)) {
                                    TypeRef written = resolve_type(map_of(p0.get(la::TYPE.code)));
                                    if (written && (written.kind() == LogosType::Kind::Ref ||
                                               written.kind() == LogosType::Kind::MutRef))
                                        written = TypeRef(written.pointee());
                                    TypeRef self_ty{impl_self_ty};
                                    if (written) {
                                        bool eq = types_equal(written, self_ty);
                                        bool eq_lt = detail::types_equal_with_lifetimes(written, self_ty);
                                        auto adt_name = [](TypeRef t) {
                                            std::string s(t.struct_name());
                                            if (s.empty()) s = std::string(t.enum_name());
                                            return s;
                                        };
                                        std::string wname = adt_name(written), sname_ = adt_name(self_ty);
                                        bool same_adt = !wname.empty() && wname == sname_ &&
                                                        written.kind() == self_ty.kind();
                                        // Rust E0308 "mismatched `self`
                                        // parameter type". An ELIDED
                                        // lifetime-argument list asserts
                                        // nothing: `self: &C` inside
                                        // `impl<'a> C<'a>` is the elision, not
                                        // a second spelling — compare the
                                        // lifetime arguments only when some
                                        // were written. PROBES.md 2026-09-04a.
                                        bool lt_written = !written.lifetime_args().empty();
                                        if (same_adt && (!eq || (!eq_lt && lt_written)))
                                            error(std::format(
                                                "mismatched `self` parameter type: expected "
                                                "'{}', found '{}'",
                                                type_str(self_ty, true), type_str(written, true)));
                                    }
                                }
                            }
                        }
                    }
                }
                // Trait-impl methods inherit their trait's accessibility:
                // if the trait is reachable, so are its methods.  The
                // grammar disallows `pub fn` inside trait / trait-impl
                // blocks, so force is_pub=true post-collection.  Inherent
                // impls (no trait_name) keep the explicit pub/private split.
                if (!trait_name.empty()) {
                    // Push method-level type params so `fn m<H: Bound>(&self, x: &mut H)`
                    // can resolve H when re-walking params for public-visibility promotion.
                    auto method_tps = read_type_params(m);
                    if (!method_tps.empty()) push_type_params(method_tps);
                    std::vector<TypeRef> method_param_types;
                    if (m.has_key(la::PARAMS)) {
                        auto params_av = m.get(la::PARAMS.code);
                        if (params_av.is_pointer()) {
                            auto params_node = map_of(params_av);
                            if (params_node.has_key(la::ITEMS)) {
                                auto arr = arr_of(params_node.get(la::ITEMS.code));
                                for (uint64_t j = 0; j < arr.size(); ++j) {
                                    auto p = map_of(arr.get(j));
                                    if (code_of(p) != la::PARAM) continue;
                                    if (p.has_key(la::TYPE))
                                        method_param_types.push_back(resolve_type(map_of(p.get(la::TYPE.code))));
                                    else {
                                        auto self_t = current_type_params_.count("Self")
                                            ? current_type_params_.at("Self") : error_t();
                                        bool is_mut = p.has_key(la::IS_MUT) &&
                                                      !p.get(la::IS_MUT.code).is_null() &&
                                                      p.get(la::IS_MUT.code).as_value<uint8_t>() != 0;
                                        method_param_types.push_back(make_ref(is_mut, self_t));
                                    }
                                }
                            }
                        }
                    }
                    if (!method_tps.empty()) pop_type_params(method_tps);
                    if (auto it = find_func_by_base_and_signature(mangled, method_param_types, false))
                        const_cast<SemaFuncInfo*>(it)->is_pub = true;
                }
                if (is_blanket) {
                    BlanketImpl bi_rec;
                    bi_rec.trait_name = trait_name;
                    bi_rec.target_typevar = target;
                    bi_rec.bound_trait = blanket_bound_trait;
                    bi_rec.extra_bounds = blanket_extra_bounds;
                    // B-mv-03: trait IDENTITIES alongside the raw spellings.
                    bi_rec.canonical_trait = blanket_trait_canonical;
                    bi_rec.canonical_bound_trait = blanket_bound_canonical;
                    bi_rec.canonical_extra_bounds = blanket_extra_canonical;
                    bi_rec.method_name = mname;
                    bi_rec.mangled_name = mangled;
                    bi_rec.primary_assoc_eqs = blanket_primary_assoc_eqs;
                    bi_rec.extra_assoc_eqs = blanket_extra_assoc_eqs;
                    if (!cur_from_binary_) user_blanket_mangled_.insert(bi_rec.mangled_name);
                    blanket_impls_.push_back(std::move(bi_rec));
                }
            } else if (code_of(m) == la::ASSOC_TYPE_IMPL && !trait_name.empty()) {
                auto aname = std::string(str_of(m.get(la::NAME.code)));
                // For blanket impls, key under the synthetic `$blanket$...`
                // name so normal `T::Assoc` lookups on other generics don't
                // shadow; the AssocType resolver falls back to blanket keys
                // when the concrete base satisfies the blanket's bound.
                std::string key_target = is_blanket
                    ? ("$blanket$" + trait_name + "$" + blanket_bound_trait + "$" + target)
                    : target;
                // G156-1: key by the trait's concrete type-args so two impls of
                // a generic trait `Trait<T>` for ONE type at distinct T (each
                // declaring the same-named assoc type) coexist. The SUFFIXED key
                // (empty suffix for non-generic traits → identical to the legacy
                // key) is always stored; the PLAIN key is stored first-impl-wins
                // for backward-compat + unambiguous single-impl lookups, and
                // ERASED once a second distinct-args impl appears so a bare
                // ambiguous `X::Assoc` lookup fails loud (Rust requires
                // `<X as Trait<T>>::Assoc`). Resolution prefers the suffixed key
                // when the trait args are known from the impl context
                // (current_impl_trait_args_). A true duplicate (same
                // trait+args+target+name) still collides on the suffixed key.
                std::string targ_sfx = trait_targ_suffix(trait_type_args);
                std::string key  = trait_name + targ_sfx + "::" + key_target + "::" + aname;
                std::string pkey = trait_name + "::" + key_target + "::" + aname;
                if (assoc_type_impls_.count(key))
                    error(std::format("impl {} for {}: duplicate associated type '{}'",
                                      trait_name, target, aname));
                // GAT: read assoc type's own params (e.g. type Item<T> = ...)
                std::vector<TypeParam> gat_tps = read_type_params(m);
                // Bug 2 fix: GAT param names must not shadow impl type param names.
                for (auto& gtp : gat_tps)
                    for (auto& itp : impl_tps)
                        if (gtp.name == itp.name)
                            error(std::format("impl {} for {}: GAT param '{}' shadows impl type param",
                                              trait_name, target, gtp.name));
                // Bug 4 fix: impl GAT arity must match the trait's declaration.
                auto tit_gat = find_trait_iter_scoped(trait_name);
                if (tit_gat != traits_.end()) {
                    for (auto& at_def : tit_gat->second.assoc_types) {
                        if (at_def.name == aname && at_def.type_params.size() != gat_tps.size()) {
                            error(std::format(
                                "impl {} for {}: associated type '{}' has {} GAT params but trait declares {}",
                                trait_name, target, aname,
                                gat_tps.size(), at_def.type_params.size()));
                            break;
                        }
                    }
                }
                push_type_params(gat_tps);
                auto atype = resolve_type(map_of(m.get(la::TYPE.code)));
                pop_type_params(gat_tps);
                if (ast_elided_ref_(map_of(m.get(la::TYPE.code))))
                    error(std::format("impl {} for {}: associated type '{}' contains a borrowed value with an elided lifetime (E0106) — name it, e.g. `impl<'a> ... {{ type {} = &'a ... }}`",
                                      trait_name, target, aname, aname));
                AssocTypeEntry ate{atype, impl_tps, gat_tps, std::move(pending_doc_)};
                pending_doc_.clear();
                assoc_type_impls_[key] = ate;
                if (!cur_from_binary_) user_assoc_type_impl_keys_.insert(key);
                // Plain key: first-impl-wins; erase on a second distinct-args
                // impl so bare ambiguous lookups fail loud (G156-1). When
                // targ_sfx is empty (non-generic trait) key==pkey already —
                // nothing more to do.
                if (!targ_sfx.empty()) {
                    if (!assoc_type_impls_.count(pkey)) {
                        assoc_type_impls_[pkey] = std::move(ate);
                        if (!cur_from_binary_) user_assoc_type_impl_keys_.insert(pkey);
                    } else {
                        // A different-args impl already claimed the plain key →
                        // the bare projection is now ambiguous. Drop it.
                        assoc_type_impls_.erase(pkey);
                        user_assoc_type_impl_keys_.erase(pkey);
                    }
                }
            } else if (code_of(m) == la::ASSOC_CONST_IMPL) {
                auto cname = std::string(str_of(m.get(la::NAME.code)));
                std::string assoc_doc = std::move(pending_doc_);
                pending_doc_.clear();
                if (trait_name.empty()) {
                    // B97: inherent assoc-const on `impl S { const C: T = ...; }`
                    // is allowed; register it under "inherent::<target>::<name>".
                    TypeRef ctype = nullptr;
                    if (m.has_key(la::TYPE))
                        ctype = resolve_type(map_of(m.get(la::TYPE.code)));
                    std::string key = "inherent::" + target + "::" + cname;
                    assoc_const_impls_[key] = { ctype, m.get(la::VALUE), nullptr, std::move(assoc_doc) };
                    if (!cur_from_binary_) user_assoc_const_impl_keys_.insert(key);
                } else {
                    TypeRef ctype = nullptr;
                    if (m.has_key(la::TYPE))
                        ctype = resolve_type(map_of(m.get(la::TYPE.code)));
                    // Type check: impl's type must match the trait's declared type.
                    auto tit2 = find_trait_iter_scoped(trait_name);
                    if (tit2 != traits_.end() && ctype) {
                        for (auto& ac_def : tit2->second.assoc_consts) {
                            if (ac_def.name == cname && ac_def.type) {
                                if (!types_equal(ac_def.type, ctype))
                                    error(std::format(
                                        "impl {} for {}: associated constant '{}' declared as '{}' but trait requires '{}'",
                                        trait_name, target, cname,
                                        type_str(ctype), type_str(ac_def.type)));
                                break;
                            }
                        }
                    }
                    std::string key = trait_name + "::" + target + "::" + cname;
                    assoc_const_impls_[key] = { ctype, m.get(la::VALUE), nullptr, std::move(assoc_doc) };
                    if (!cur_from_binary_) user_assoc_const_impl_keys_.insert(key);
                }
            }
        }
        // Defensive: clear pending_doc_ at end of impl-body iteration so
        // it doesn't leak into the surrounding collect_module loop.
        pending_doc_.clear();
    }
    // If this is a blanket impl with no fn methods (only assoc-types), the
    // items loop didn't push anything to blanket_impls_. Push a marker
    // entry now so trait-satisfaction queries (sema_has_impl_recursive,
    // assoc_eqs_satisfied) can see it. method_name stays empty.
    if (is_blanket && blanket_impls_.size() == blanket_size_before) {
        BlanketImpl bi_rec;
        bi_rec.trait_name = trait_name;
        bi_rec.target_typevar = target;
        bi_rec.bound_trait = blanket_bound_trait;
        bi_rec.extra_bounds = blanket_extra_bounds;
        // B-mv-03: trait IDENTITIES alongside the raw spellings.
        bi_rec.canonical_trait = blanket_trait_canonical;
        bi_rec.canonical_bound_trait = blanket_bound_canonical;
        bi_rec.canonical_extra_bounds = blanket_extra_canonical;
        // method_name / mangled_name intentionally empty — this is a
        // satisfaction-only marker, not a method-dispatch entry.
        bi_rec.primary_assoc_eqs = blanket_primary_assoc_eqs;
        bi_rec.extra_assoc_eqs = blanket_extra_assoc_eqs;
        // M5 step 5c: for satisfaction markers (empty mangled), tag with
        // a synthetic "$marker$<trait>$<bound>$<target>" so the snapshot
        // filter can drop user-origin entries by mangled-name.
        if (!cur_from_binary_) {
            std::string marker = "$marker$" + trait_name + "$" +
                                 blanket_bound_trait + "$" + target;
            user_blanket_mangled_.insert(marker);
            bi_rec.mangled_name = std::move(marker);
        }
        blanket_impls_.push_back(std::move(bi_rec));
    }

    // Check completeness: every required trait method must be in the impl.
    // Default methods are registered as Target__method if not overridden.
    // Blanket impls use a synthetic target in their registrations; apply the
    // same mapping here so the completeness check sees the real methods.
    std::string check_target = is_blanket
        ? ("$blanket$" + trait_name + "$" + blanket_bound_trait + "$" + target)
        : target;
    if (!trait_name.empty()) {
        auto tit = find_trait_iter_scoped(trait_name);
        if (tit != traits_.end()) {
            for (auto& m : tit->second.methods) {
                auto mangled = check_target + "__" + m.name;
                // Trait-aware mangling: a method that collided with another
                // trait's same-named method was re-keyed under the
                // trait-qualified base; check that first.
                // G156-1: when this impl has concrete trait type-args, the method
                // was re-keyed under the args-aware base `<tgt>__<Trait>$<args>__<m>`
                // (so `impl Trait<u64>` and `impl Trait<u8>` coexist). Look that up
                // first, then the bare trait-qualified base, then the plain name.
                std::string targ_sfx = trait_targ_suffix(trait_type_args);
                auto cands = !targ_sfx.empty()
                    ? find_func_candidates(check_target + "__" + trait_name + targ_sfx + "__" + m.name)
                    : std::vector<const SemaFuncInfo*>{};
                if (cands.empty()) cands = find_func_candidates(
                    check_target + "__" + trait_name + "__" + m.name);
                if (cands.empty()) cands = find_func_candidates(mangled);
                // Find if THIS specific overload was explicitly provided.
                // Match by arity and non-receiver param types.
                // Receiver (param[0]) is &TypeVar(Self) in the trait vs
                // &ConcreteType in the impl — always skip it.
                // A TypeVar in the trait param (possibly inside Ref/Ptr) is a
                // generic param and matches any concrete impl type.
                auto is_generic_param = [](TypeRef t) -> bool {
                    while (t && (TypeRef(t).kind() == LogosType::Kind::Ref ||
                                 TypeRef(t).kind() == LogosType::Kind::MutRef ||
                                 TypeRef(t).kind() == LogosType::Kind::Ptr))
                        t = TypeRef(t).pointee();
                    if (!t) return false;
                    // TypeVar = generic type param (T); AssocType = T::Item
                    // Both are polymorphic from the trait's perspective and
                    // match any concrete type in the impl.
                    TypeRef tv{t};
                    return tv.kind() == LogosType::Kind::TypeVar ||
                           tv.kind() == LogosType::Kind::AssocType;
                };
                // Fn-family epic step B: detect a variadic trait type
                // param (`pub trait Fn<A...> { fn call(&self, args: A...)
                // ... }`). If the trait method uses the pack TypeVar at
                // some position, the impl is allowed to expose any number
                // of concrete params from that position onward — pack
                // absorbs them.
                //
                // Step B.4 (Deferred-3): per-element type check past the
                // pack position. The impl block carries `trait_type_args`
                // (e.g. `impl Fn<i32, i32> for Foo` → [i32, i32]); we
                // unify each post-pack impl-method param against the
                // corresponding `trait_type_args` element so an impl that
                // exposes `fn call(&self, x: u8, y: bool)` against
                // `Fn<i32, i32>` gets rejected instead of silently bound.
                std::string variadic_tp_name;
                for (auto& tp : tit->second.type_params) {
                    if (tp.is_variadic) { variadic_tp_name = tp.name; break; }
                }
                // S2b: substitute the impl's CONCRETE trait args into the
                // declared method signature before comparing. `fn f(&self,
                // cnt: &[u64; N])` in `trait T<const N: u32>` must compare as
                // `&[u64; 1]` against an `impl T<K, 1>` method — without the
                // substitution a const-param-sized array in a trait method
                // makes every impl method "missing" (the conformance check is
                // one more site of the array-length disease). Bound TYPE
                // params substitute too — the leftover-generic skips below
                // still cover the unbound ones.
                SemaSubst trait_arg_subst;
                {
                    auto& tps = tit->second.type_params;
                    for (size_t ti = 0; ti < tps.size() && ti < trait_type_args.size(); ++ti) {
                        if (trait_type_args[ti])
                            trait_arg_subst[tps[ti].name] = trait_type_args[ti];
                    }
                }
                // `Self` too: without it every `&Self` parameter is skipped
                // WHOLE by is_generic_param below and the impl may declare any
                // type in that slot. PROBES.md 2026-09-04y.
                if (impl_self_ty) trait_arg_subst["Self"] = impl_self_ty;
                std::string self_mismatch_note;
                const SemaFuncInfo* matching = nullptr;
                // PROBES.md 2026-09-05z: a candidate reached the signature compare.
                bool _sigdef_arity_seen = false;
                // PROBES.md 2026-09-06a: the trait declares exactly one method
                // of this name, so no other declaration can be the impl's real
                // subject. Without it a legal same-name overload is refused.
                bool _sigdef_name_unique = true;
                {
                    int _nsame = 0;
                    for (auto& _mm : tit->second.methods)
                        if (_mm.name == m.name) ++_nsame;
                    _sigdef_name_unique = (_nsame == 1);
                }
                for (auto* c : cands) {
                    int variadic_pos = -1;
                    if (!variadic_tp_name.empty()) {
                        for (size_t k = 1; k < m.param_types.size(); ++k) {
                            auto tp = m.param_types[k];
                            if (tp &&
                                TypeRef(tp).kind() == LogosType::Kind::TypeVar &&
                                TypeRef(tp).type_var_name() == variadic_tp_name) {
                                variadic_pos = (int)k;
                                break;
                            }
                        }
                    }
                    bool has_pack = (variadic_pos >= 0);
                    if (has_pack) {
                        if (c->param_types.size() < (size_t)variadic_pos) continue;
                    } else {
                        if (c->param_types.size() != m.param_types.size()) continue;
                    }
                    _sigdef_arity_seen = true;
                    bool sig_match = true;
                    // PROBE sigalphaw/sigalphas/sigalphapar/sigalpharet — M-SIG step 2. PROBES.md.
                    std::vector<std::pair<std::string,std::string>> _amap;
                    // LANDED 2026-09-07b (was PROBE sigsubs): the trait's slot 0
                    // is SUBSTITUTED before the alpha compare, and elided pairs
                    // only with elided. Control revert = `git revert`. PROBES.md.
                    const bool _asub = true;
                    bool _astrict = logos::probe::on("sigalphas") || _asub;
                    bool _apar = logos::probe::on("sigalphaw") ||
                                 logos::probe::on("sigalphas") ||
                                 logos::probe::on("sigalphapar") || _asub;
                    bool _aret = logos::probe::on("sigalphaw") ||
                                 logos::probe::on("sigalphas") ||
                                 logos::probe::on("sigalpharet") || _asub;
                    auto _acollect = [](TypeRef t, std::vector<std::string>& o,
                                        auto& self) -> void {
                        if (!t) return;
                        using K2 = LogosType::Kind;
                        switch (TypeRef(t).kind()) {
                            case K2::Ref: case K2::MutRef:
                                o.push_back(std::string(std::string_view(TypeRef(t).lifetime())));
                                self(TypeRef(t).pointee(), o, self); break;
                            case K2::Ptr:
                                self(TypeRef(t).pointee(), o, self); break;
                            case K2::Slice: case K2::Array:
                                o.push_back(std::string(std::string_view(TypeRef(t).lifetime())));
                                self(TypeRef(t).elem(), o, self); break;
                            case K2::TraitObject: case K2::DstRef:
                                o.push_back(std::string(std::string_view(TypeRef(t).lifetime())));
                                break;
                            case K2::Tuple:
                                for (auto e : TypeRef(t).tuple_elems()) self(e, o, self);
                                break;
                            case K2::FnPtr: case K2::Closure:
                                for (auto p : TypeRef(t).closure_params()) self(p, o, self);
                                self(TypeRef(t).closure_ret(), o, self); break;
                            case K2::AssocType:
                                self(TypeRef(t).assoc_base(), o, self);
                                for (auto g : TypeRef(t).gat_args()) self(g, o, self);
                                for (auto& l : TypeRef(t).lifetime_args()) o.push_back(l);
                                break;
                            default:
                                for (auto a : TypeRef(t).type_args()) self(a, o, self);
                                for (auto& l : TypeRef(t).lifetime_args()) o.push_back(l);
                                break;
                        }
                    };
                    // Rust ELISION, expanded per signature before the alpha
                    // compare: fresh binder per elided input, an elided output
                    // takes the receiver's. LANDED 2026-09-09elide. PROBES.md.
                    auto _esyn = [](size_t slot, size_t idx) -> std::string {
                        return std::format("#e{}_{}", slot, idx);
                    };
                    // Filled once `check_end` is known; "" = not expandable.
                    std::string _eout_a, _eout_b;
                    auto _eout_of = [&](const std::vector<TypeRef>& ps,
                                        size_t upto) -> std::string {
                        using K5 = LogosType::Kind;
                        if (m.has_self_receiver && !ps.empty() && ps[0] &&
                            (TypeRef(ps[0]).kind() == K5::Ref ||
                             TypeRef(ps[0]).kind() == K5::MutRef)) {
                            std::vector<std::string> ls;
                            _acollect(ps[0], ls, _acollect);
                            if (!ls.empty())
                                return ls[0].empty() ? _esyn(0, 0) : ls[0];
                        }
                        std::string only; size_t n = 0;
                        for (size_t k = 0; k < upto && k < ps.size(); ++k) {
                            std::vector<std::string> ls;
                            _acollect(ps[k], ls, _acollect);
                            for (size_t i = 0; i < ls.size(); ++i) {
                                ++n;
                                only = ls[i].empty() ? _esyn(k, i) : ls[i];
                            }
                        }
                        return n == 1 ? only : std::string();
                    };
                    auto _alpha_ok = [&](TypeRef ta, TypeRef tb, size_t slot,
                                         bool is_ret) -> bool {
                        std::vector<std::string> la, lb;
                        _acollect(ta, la, _acollect);
                        _acollect(tb, lb, _acollect);
                        if (la.size() != lb.size()) return false;
                        for (size_t i = 0; i < la.size(); ++i) {
                            std::string x = la[i];
                            std::string y = lb[i];
                            if (x.empty()) x = is_ret ? _eout_a : _esyn(slot, i);
                            if (y.empty()) y = is_ret ? _eout_b : _esyn(slot, i);
                            if (x.empty() || y.empty()) {
                                // Elision not expandable here — the old rule stands.
                                if (_astrict && !(x.empty() && y.empty())) return false;
                                continue;
                            }
                            if (x == "static" || y == "static") {
                                if (x != y) return false;
                                continue;
                            }
                            bool bound = false;
                            for (auto& pr : _amap) {
                                if (pr.first == x) { if (pr.second != y) return false; bound = true; }
                                else if (pr.second == y) { return false; }
                            }
                            if (!bound) _amap.emplace_back(x, y);
                        }
                        return true;
                    };
                    // A DST-alias `Self` (`str` resolves to `[u8]` here while the
                    // impl's WRITTEN `str` resolves to `&[u8]`) makes the two
                    // sides differ by a reference layer at WHATEVER slot it
                    // appears in — receiver, parameter or return. A
                    // representation difference, never a lifetime fact: the
                    // alpha check declines to speak. PROBES.md 2026-09-07b.
                    auto _self_shape_artefact = [&](TypeRef raw, TypeRef sub,
                                                    TypeRef impl_t) -> bool {
                        using K3 = LogosType::Kind;
                        if (!raw || !sub || !impl_t) return false;
                        TypeRef bare = raw;
                        while (bare && (TypeRef(bare).kind() == K3::Ref ||
                                        TypeRef(bare).kind() == K3::MutRef))
                            bare = TypeRef(bare).pointee();
                        if (!bare || TypeRef(bare).kind() != K3::TypeVar ||
                            std::string_view(TypeRef(bare).type_var_name()) != "Self")
                            return false;
                        // No `Self` to substitute (a non-nominal impl target the
                        // chain above cannot rebuild) — the trait slot is a
                        // TypeVar the impl slot can never equal, and a comparator
                        // that speaks here states a FALSEHOOD. It declines.
                        if (!impl_self_ty) return true;   // nothing to substitute
                        // The alpha comparator's subject is LIFETIMES. When the
                        // substituted `Self` and the impl's written slot are not
                        // even the same type modulo lifetimes, the substitution
                        // and the impl's spelling disagree about the SHAPE of
                        // Self (`str` -> `[u8]` vs the written `&str` -> `&[u8]`;
                        // `&mut Self` -> `&mut i64` vs the written `&mut &mut i64`)
                        // and every sentence this check could print is false. The
                        // type compare owns that verdict; this one declines.
                        // DOOR 1 of `trait.impl.method-receiver-conforms`: the
                        // hatch covers a SAME-PREFIX representation difference
                        // only. PROBES.md 2026-09-10a-selfrecvland.
                        {
                            auto _pfx = [](TypeRef t) {
                                std::string s;
                                while (t && (TypeRef(t).kind() == K3::Ref ||
                                             TypeRef(t).kind() == K3::MutRef)) {
                                    s += (TypeRef(t).kind() == K3::MutRef) ? 'm' : 'r';
                                    t = TypeRef(t).pointee();
                                }
                                return s;
                            };
                            if (_pfx(sub) != _pfx(impl_t)) return false;
                        }
                        return !types_equal(sub, impl_t);
                    };
                    size_t check_end = has_pack
                        ? (size_t)variadic_pos
                        : m.param_types.size();
                    // The elided-output binder per side, over the compared
                    // INPUT slots, the trait's substituted first. PROBES.md.
                    {
                        std::vector<TypeRef> _etr(m.param_types);
                        if (!trait_arg_subst.empty())
                            for (auto& _t : _etr)
                                if (_t) _t = subst_type_sema(_t, trait_arg_subst);
                        _eout_a = _eout_of(_etr, check_end);
                        _eout_b = _eout_of(c->param_types, check_end);
                    }
                    // PROBE sigselflt — M-SIG(a), the k=0 slot the loop below
                    // skips. DECLINED 2026-09-02w: it refuses a legal
                    // ALPHA-RENAMING of a method binder. See PROBES.md.
                    if (logos::probe::on("sigselflt") && sig_match &&
                        !m.param_types.empty() && !c->param_types.empty() &&
                        m.param_types[0] && c->param_types[0]) {
                        TypeRef ts = m.param_types[0], cs = c->param_types[0];
                        using K = LogosType::Kind;
                        if ((TypeRef(ts).kind() == K::Ref ||
                             TypeRef(ts).kind() == K::MutRef) &&
                            TypeRef(cs).kind() == TypeRef(ts).kind() &&
                            std::string_view(TypeRef(ts).lifetime()) !=
                            std::string_view(TypeRef(cs).lifetime()) &&
                            !std::string_view(TypeRef(ts).lifetime()).empty())
                            sig_match = false;
                        if (sig_match &&
                            (TypeRef(ts).kind() == K::Ref ||
                             TypeRef(ts).kind() == K::MutRef) &&
                            TypeRef(cs).kind() == TypeRef(ts).kind() &&
                            std::string_view(TypeRef(ts).lifetime()).empty() &&
                            !std::string_view(TypeRef(cs).lifetime()).empty())
                            sig_match = false;
                    }
                    if (_apar && sig_match && !m.param_types.empty() &&
                        !c->param_types.empty() && m.param_types[0] &&
                        c->param_types[0]) {
                        TypeRef _t0 = m.param_types[0];
                        bool _t0_collapsed = false;
                        if (_asub && !trait_arg_subst.empty()) {
                            _t0 = subst_type_sema(m.param_types[0], trait_arg_subst);
                            _t0_collapsed = _self_shape_artefact(
                                m.param_types[0], _t0, c->param_types[0]);
                        }
                        // DOOR 2 of `trait.impl.method-receiver-conforms`, in
                        // SERIES with door 1. PROBES.md 2026-09-10a-selfrecvland.
                        if (!_t0_collapsed &&
                            (!_alpha_ok(_t0, c->param_types[0], 0, false) ||
                             !types_equal(_t0, c->param_types[0]))) {
                            if (_asub && self_mismatch_note.empty())
                                self_mismatch_note = std::format(
                                    "the receiver is declared '{}' and the impl "
                                    "declares '{}'", type_str(_t0),
                                    type_str(c->param_types[0]));
                            sig_match = false;
                        }
                    }
                    for (size_t k = 1; sig_match && k < check_end; ++k) {
                        auto tp = m.param_types[k];
                        auto cp = c->param_types[k];
                        if (!tp || !cp) { sig_match = false; break; }
                        if (!trait_arg_subst.empty())
                            tp = subst_type_sema(tp, trait_arg_subst);
                        // If the trait param is generic (TypeVar or AssocType),
                        // it matches any concrete impl type.
                        if (is_generic_param(tp)) continue;
                        if (is_generic_param(cp)) continue;
                        // PROBE sigparamlt — M-SIG(c), DECLINED 2026-09-02w:
                        // it refuses a legal ALPHA-RENAMING. See PROBES.md.
                        if (logos::probe::on("sigparamlt") &&
                            !detail::types_equal_with_lifetimes(tp, cp))
                            { sig_match = false; break; }
                        if (!types_equal(tp, cp)) {
                            // Only reachable for a `Self`-shaped trait slot
                            // because of the substitution above.
                            if (self_mismatch_note.empty() &&
                                is_generic_param(m.param_types[k]))
                                self_mismatch_note = std::format(
                                    "parameter {} is declared '{}' and the impl "
                                    "declares '{}'", k, type_str(tp), type_str(cp));
                            sig_match = false; break;
                        }
                        if (_apar && !(_asub && _self_shape_artefact(m.param_types[k], tp, cp))
                            && !_alpha_ok(tp, cp, k, false)) {
                            if (_asub && self_mismatch_note.empty())
                                self_mismatch_note = std::format(
                                    "parameter {} is declared '{}' and the impl "
                                    "declares '{}'", k, type_str(tp), type_str(cp));
                            sig_match = false; break;
                        }
                    }
                    // Per-element check past the pack position: each
                    // impl-method param at index k (where k >=
                    // variadic_pos) corresponds to trait_type_args[k -
                    // variadic_pos]. Mismatch rejects the candidate.
                    if (has_pack && sig_match) {
                        for (size_t k = (size_t)variadic_pos;
                             k < c->param_types.size(); ++k) {
                            size_t targ_idx = k - (size_t)variadic_pos;
                            if (targ_idx >= trait_type_args.size()) {
                                // Impl exposes more args than the pack
                                // instantiation has — concrete arity
                                // overflow.
                                sig_match = false; break;
                            }
                            auto exp = trait_type_args[targ_idx];
                            auto cp  = c->param_types[k];
                            if (!exp || !cp) continue;
                            if (is_generic_param(exp)) continue;
                            if (is_generic_param(cp)) continue;
                            if (!types_equal(exp, cp)) {
                                sig_match = false; break;
                            }
                        }
                        // Trait pack carries more args than impl exposes —
                        // arity underflow (less common; the
                        // `c->param_types.size() < variadic_pos` guard
                        // catches the leading-args case but not the
                        // pack-itself-arity).
                        if (sig_match &&
                            (c->param_types.size() - (size_t)variadic_pos)
                                != trait_type_args.size())
                            sig_match = false;
                    }
                    // PROBE sigretlt — M-SIG(b), the return type nothing
                    // compares. DECLINED 2026-09-02w at cost 1099. See PROBES.md.
                    if (sig_match && logos::probe::on("sigretlt") &&
                        m.ret_type && c->ret_type) {
                        TypeRef tr = m.ret_type;
                        if (!trait_arg_subst.empty())
                            tr = subst_type_sema(tr, trait_arg_subst);
                        if (!is_generic_param(tr) &&
                            !is_generic_param(c->ret_type) &&
                            !detail::types_equal_with_lifetimes(tr, c->ret_type))
                            sig_match = false;
                    }
                    // LANDED 2026-09-09sigland: the return slot is compared BY
                    // TYPE, not only by `_alpha_ok`'s lifetime strings. PROBES.md.
                    if (sig_match && _aret && m.ret_type && c->ret_type) {
                        TypeRef tra = m.ret_type;
                        if (!trait_arg_subst.empty())
                            tra = subst_type_sema(tra, trait_arg_subst);
                        if (!is_generic_param(tra) &&
                            !is_generic_param(c->ret_type) &&
                            !(_asub && _self_shape_artefact(m.ret_type, tra, c->ret_type)) &&
                            (!_alpha_ok(tra, c->ret_type, check_end, true) ||
                             !types_equal(tra, c->ret_type))) {
                            if (_asub && self_mismatch_note.empty())
                                self_mismatch_note = std::format(
                                    "the return type is declared '{}' and the "
                                    "impl declares '{}'", type_str(tra),
                                    type_str(c->ret_type));
                            sig_match = false;
                        }
                    }
                    if (sig_match) { matching = c; break; }
                }
                if (matching) {
                    if (early_bound_lts_(m.lifetime_params, m.lifetime_outlives, m.type_params) !=
                        early_bound_lts_(matching->lifetime_params, matching->lifetime_outlives, matching->type_params))
                        error(std::format("impl {} for {}: lifetime parameters or bounds on method '{}' do not match the trait declaration (E0195)",
                                          trait_name, target, m.name));
                    if (m.is_unsafe != matching->is_unsafe) {
                        error(std::format("impl {} for {}: method '{}' has mismatched unsafe parity (trait: {}, impl: {})",
                            trait_name, target, m.name,
                            m.is_unsafe ? "unsafe" : "safe",
                            matching->is_unsafe ? "unsafe" : "safe"));
                    }
                } else if (m.has_default && _sigdef_arity_seen &&
                           _sigdef_name_unique) {
                    // A default body does NOT exempt an override from the
                    // conformance check. PROBES.md 2026-09-06a.
                    if (!self_mismatch_note.empty())
                        error(std::format("impl {} for {}: method '{}' does not match "
                              "the trait declaration: {}",
                              trait_name, target, m.name, self_mismatch_note));
                    else
                        error(std::format("impl {} for {}: method '{}' does not match the trait declaration's signature",
                              trait_name, target, m.name));
                } else if (m.has_default) {
                    // This overload not explicitly provided; register the default.
                    // Build Self type; for generic impls include the type params as TypeVars.
                    TypeRef self_type = nullptr;
                    {
                        auto [spkg_def, ssi_def] = find_struct_by_name(target);
                        auto [dpkg_def, dsi_def] = find_datatype_by_name(target);
                        auto _shaped_target = [](TypeRef pat) -> bool {
                            if (!pat) return false;
                            for (auto a : TypeRef(pat).type_args()) {
                                if (!a) continue;
                                auto k = TypeRef(a).kind();
                                if (k != LogosType::Kind::TypeVar &&
                                    k != LogosType::Kind::ConstVar)
                                    return true;
                            }
                            return false;
                        };
                        if (ssi_def) {
                            // Shaped target → Self = the impl's pattern (see
                            // sema_decl synthesis; both sides must agree or
                            // declared vs body types of defaults diverge).
                            if (target_resolved && _shaped_target(target_resolved)) {
                                self_type = target_resolved;
                            } else if (!impl_tps.empty()) {
                                std::vector<TypeRef> tv_args;
                                for (auto& tp : impl_tps)
                                    tv_args.push_back(make_typevar(tp.name));
                                self_type = make_generic_struct(
                                    target, std::move(tv_args),
                                    self_lt_args_(ssi_def->lifetime_params),
                                    spkg_def);
                            } else if (!ssi_def->lifetime_params.empty()) {
                                // The SAME defect as at the impl header above,
                                // in the synthesis of an inherited default.
                                logos::probe::census("self.lts-restored.default");
                                self_type = make_generic_struct(target, {},
                                                                self_lt_args_(ssi_def->lifetime_params),
                                                                spkg_def);
                            } else {
                                self_type = make_struct_type(target, spkg_def);
                            }
                        } else if (dsi_def) {
                            if (target_resolved && _shaped_target(target_resolved)) {
                                self_type = target_resolved;
                            } else if (!impl_tps.empty()) {
                                std::vector<TypeRef> tv_args;
                                for (auto& tp : impl_tps)
                                    tv_args.push_back(make_typevar(tp.name));
                                self_type = make_generic_datatype(target, std::move(tv_args), {}, dpkg_def);
                            } else {
                                self_type = make_datatype_type(target, dpkg_def);
                            }
                        } else if (auto prim_t = lookup_type_by_name(target);
                                   prim_t && [&]{
                                       auto k = TypeRef(prim_t).kind();
                                       using K = LogosType::Kind;
                                       return k==K::I8||k==K::I16||k==K::I24||k==K::I32||
                                              k==K::I56||k==K::I64||k==K::I128||
                                              k==K::U8||k==K::U16||k==K::U24||k==K::U32||
                                              k==K::U56||k==K::U64||k==K::U128||
                                              k==K::Usize||k==K::Isize||
                                              k==K::F32||k==K::F64||k==K::Bool||k==K::Char;
                                   }()) {
                            // G160-3: `impl Trait for <SCALAR primitive>` (i64/
                            // u32/bool/char/…). Bind Self to the primitive so a
                            // default body's `&Self` signature resolves. Without
                            // this, every inherited default beyond the first
                            // (which only worked by inheriting a leaked Self)
                            // failed with "unknown type 'Self'". RESTRICTED to
                            // scalar kinds — `str` (a slice alias) / enum targets
                            // (Option/Result) must NOT be bound here (their
                            // defaults rely on Self staying a TypeVar for generic
                            // eq inference).
                            self_type = prim_t;
                        }
                    }
                    // Blanket impl `impl<T: Bound> Trait for T {}`: the default
                    // method must be synthesized as a generic fn under the
                    // synthetic `$blanket$...` target (so dispatch can find it)
                    // with Self = the blanket TypeVar, and a blanket_impls_
                    // entry pushed so try_blanket_method_dispatch surfaces it on
                    // any concrete receiver satisfying Bound. Without this, the
                    // trait's defaults are invisible on a blanket impl.
                    std::string def_reg_target = is_blanket ? check_target : target;
                    // PROBE implanonx — src/compiler/PROBES.md 2026-09-13a: an inherited default's
                    // Self takes the impl header's NAMED anonymous regions (renamed above), not the
                    // positional binder list, where a header `'_` has no entry.
                    if (logos::probe::on("implanonx") && !is_blanket && self_type && impl_self_ty &&
                        TypeRef(self_type).kind() == TypeRef(impl_self_ty).kind() &&
                        (TypeRef(self_type).kind() == LogosType::Kind::Struct ||
                         TypeRef(self_type).kind() == LogosType::Kind::ZonedStruct ||
                         TypeRef(self_type).kind() == LogosType::Kind::Enum)) {
                        logos::probe::census("implanonx.default.site");
                        std::vector<std::string> sl_ = TypeRef(self_type).lifetime_args();
                        std::vector<std::string> il_ = TypeRef(impl_self_ty).lifetime_args();
                        bool same_ = TypeRef(self_type).kind() == LogosType::Kind::Enum
                            ? TypeRef(self_type).enum_name() == TypeRef(impl_self_ty).enum_name()
                            : TypeRef(self_type).struct_name() == TypeRef(impl_self_ty).struct_name();
                        bool any_ = false;
                        if (same_ && sl_.size() == il_.size())
                            for (size_t i_ = 0; i_ < sl_.size(); ++i_)
                                if (sl_[i_].empty() && il_[i_].rfind("'__anon", 0) == 0) { sl_[i_] = il_[i_]; any_ = true; }
                        if (any_) {
                            logos::probe::census("implanonx.default.renamed");
                            std::vector<TypeRef> ta_;
                            for (auto a_ : TypeRef(self_type).type_args()) ta_.push_back(a_);
                            self_type = TypeRef(self_type).kind() == LogosType::Kind::Enum
                                ? make_generic_enum(TypeRef(self_type).enum_name(), std::move(ta_), std::move(sl_), TypeRef(self_type).pkg_name())
                                : make_generic_struct(TypeRef(self_type).struct_name(), std::move(ta_), std::move(sl_), TypeRef(self_type).pkg_name());
                        }
                    }
                    if (is_blanket)
===
name: selfvee
file: src/compiler/sema_decl.cpp
---
    //     (explicit methods; default methods are already seeded — G156-13)
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
---
    //     (explicit methods; default methods are already seeded — G156-13)
    // PROBE implanon / selfbody / selfv / selfvg — src/compiler/PROBES.md 2026-09-13a.
    TypeRef sb_self_ = nullptr;
    if (((logos::probe::on("implanon") || logos::probe::on("selfv") || logos::probe::on("selfvg") || logos::probe::on("implanonx") || logos::probe::on("selfvee") || logos::probe::on("selfveu")) || (logos::probe::on("selfbody") || logos::probe::on("selfv") || logos::probe::on("selfvg") || logos::probe::on("selfvee") || logos::probe::on("selfveu"))) && node.has_key(la::TYPE)) {
        auto wn_ = map_of(node.get(la::TYPE.code));
        TypeRef wr_ = target_resolved;
        if (!wr_ && code_of(wn_) == la::GENERIC_INST) { logos::probe::census("selfbody.resolve"); wr_ = resolve_type(wn_); }
        if (wr_ && (logos::probe::on("implanon") || logos::probe::on("selfv") || logos::probe::on("selfvg") || logos::probe::on("implanonx") || logos::probe::on("selfvee") || logos::probe::on("selfveu"))) {
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
        if (wr_ && (logos::probe::on("selfbody") || logos::probe::on("selfv") || logos::probe::on("selfvg") || logos::probe::on("selfvee") || logos::probe::on("selfveu")) && (impl_tps.empty() || logos::probe::on("selfvg")) &&
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
===
name: selfveu
file: src/compiler/sema_expr.cpp
---
                }
                if (changed) {
                    logos::probe::census("subst.structlit.gen.differs");
                    lit_type = slit_is_zoned
                        ? make_generic_datatype(std::string(sname), args, nl)
                        : make_generic_struct(std::string(sname), args, nl);
                }
            }
        }
        // B77: verify generic-struct's `where 'a: 'b` constraints.
        check_struct_lit_outlives(std::string(sname),
                                  sinfo.lifetime_params,
                                  sinfo.lifetime_outlives,
                                  TypeRef(lit_type).lifetime_args(),
                                  sinfo.fields,
                                  fields);

        return builder().struct_lit(concrete, std::move(fields), lit_type);
    }

    // Non-generic struct: validate against template fields directly.
    StrMap<bool> initialized;
    for (auto& f : sinfo.fields) initialized[std::string(f.name)] = false;
    for (auto& [fname, fval] : fields) {
        auto it = initialized.find(fname);
        if (it == initialized.end()) {
            // Check variadic
            bool matched_variadic = false;
            for (auto& f : sinfo.fields) {
                if (f.is_variadic && fname.starts_with(f.name) && fname.size() > f.name.size() + 1 && fname[f.name.size()] == '_') {
                    initialized[std::string(f.name)] = true;
                    matched_variadic = true;
                    auto ft = f.type;
                    if (ft)
                        expect_type(fval, ft, CoercePos::StructLitField,
                                    std::format("struct literal '{}' field '{}':",
                                                sname, fname));
                    if (ft && TypeRef(ft).kind() != LogosType::Kind::Error &&
                        TypeRef(expr_type(fval)).kind() == LogosType::Kind::IntLit)
                        if (auto v = get_intlit_value(fval))
                            if (!intlit_fits(*v, TypeRef(ft).kind()))
                                error(std::format("struct literal '{}' field '{}': value {} does not fit in {}",
                                      sname, fname, *v, type_str(ft)));
                    break;
                }
            }
            if (!matched_variadic)
                error(std::format("struct literal '{}': unknown field '{}'", sname, fname));
        } else {
            if (it->second) {
                error(std::format("struct literal '{}': duplicate field '{}'", sname, fname));
                continue;
            }
            it->second = true;
            auto ft = field_type_of(std::string(sname), fname);
            // Same as the generic path above: a field initializer is a write
            // to a typed place and gets a place write's coercions.
            if (ft)
                expect_type(fval, ft, CoercePos::StructLitField,
                            std::format("struct literal '{}' field '{}':",
                                        sname, fname));
            // B68.2: variance check at struct-lit field-init coercion.
            // Permissive — struct's lifetime args are inferred at this site.
            // PROBE ltmintinst: and "inferred at this site" is exactly why the
            // struct's OWN binders are not regions to compare against here —
            // see the generic path above for the measurement
            // (regions-mock-codegen). THIS is the site a struct with lifetime
            // params but no TYPE params takes.
            TypeRef ft_cmp2 = ft;
            if (ft && (logos::probe::arm_inst() || logos::probe::arm_subst())) {
                auto [_slp_pkg, _slp] = find_struct_by_name(std::string(sname));
                (void)_slp_pkg;
                if (_slp && !_slp->lifetime_params.empty()) {
                    auto blift2 = structlit_lt_subst_(_slp->lifetime_params,
                                                      _slp->fields, fields,
                                                      _slp->package.empty()
                                                        ? std::string(sname)
                                                        : _slp->package + "." + std::string(sname));
                    if (!blift2.empty()) {
                        ft_cmp2 = subst_type_sema(ft, {}, blift2);
                        if (ft_cmp2 != ft) logos::probe::census("structlit.field.instantiated");
                    }
                }
            }
            if (ft)
                check_variance(expr_type(fval), ft_cmp2,
                               std::format("struct literal '{}' field '{}'", sname, fname),
                               /*permissive=*/true);
            // T1-12 (audit-v2): dyn+auto bound at field-init coercion
            // (`Holder { d: &not_send }` against `d: &dyn Trait + Send`).
            if (ft)
                check_dyn_auto_bounds_at_coercion(fval, ft);
            // Check IntLit field value fits in the declared field type.
            if (ft && TypeRef(expr_type(fval)).kind() == LogosType::Kind::IntLit)
                if (auto v = get_intlit_value(fval))
                    if (!intlit_fits(*v, TypeRef(ft).kind()))
                        error(std::format("struct literal '{}' field '{}': value {} does not fit in {}",
                              sname, fname, *v, type_str(ft)));
            // Check array literal elements against narrow array field type.
            if (ft && TypeRef(ft).kind() == LogosType::Kind::Array && TypeRef(ft).elem() &&
                TypeRef(expr_type(fval)).kind() == LogosType::Kind::Array) {
                auto vr = expr_ref_of(fval);
                if (vr.kind() == lir_schema::expr::Code::ArrLit) {
                    lir_view::EArrLitView al{vr};
                    for (uint64_t i = 0; i < al.count(); ++i) {
                        auto el = al.elem(i);
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (!intlit_fits(*v, TypeRef(ft).elem().kind()))
                                    error(std::format("struct literal '{}' field '{}': array element {}: value {} does not fit in {}",
                                          sname, fname, i, *v, type_str(TypeRef(ft).elem())));
                    }
                }
            }
            // Check tuple literal elements against narrow tuple field element types.
            if (ft && TypeRef(ft).kind() == LogosType::Kind::Tuple && TypeRef(expr_type(fval)).kind() == LogosType::Kind::Tuple) {
                auto vr = expr_ref_of(fval);
                if (vr.kind() == lir_schema::expr::Code::TupleLit) {
                    lir_view::ETupleLitView tl{vr};
                    uint64_t i = 0;
                    tl.each_elem([&](lir_view::ExprRef el) {
                        if (i >= TypeRef(ft).tuple_elems().size()) { ++i; return; }
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (TypeRef(ft).tuple_elems()[i] && !intlit_fits(*v, TypeRef(TypeRef(ft).tuple_elems()[i]).kind()))
                                    error(std::format("struct literal '{}' field '{}': tuple element {}: value {} does not fit in {}",
                                          sname, fname, i, *v, type_str(TypeRef(ft).tuple_elems()[i])));
                        if (TypeRef(ft).tuple_elems()[i] && TypeRef(TypeRef(ft).tuple_elems()[i]).kind() == LogosType::Kind::Array &&
                            TypeRef(TypeRef(ft).tuple_elems()[i]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                            el.kind() == lir_schema::expr::Code::ArrLit) {
                            lir_view::EArrLitView ial{el};
                            for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                                auto iel = ial.elem(ii);
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (!intlit_fits(*v, TypeRef(TypeRef(ft).tuple_elems()[i]).elem().kind()))
                                            error(std::format("struct literal '{}' field '{}': tuple element {}: array element {}: value {} does not fit in {}",
                                                  sname, fname, i, ii, *v, type_str(TypeRef(TypeRef(ft).tuple_elems()[i]).elem())));
                            }
                        }
                        if (TypeRef(ft).tuple_elems()[i] && TypeRef(TypeRef(ft).tuple_elems()[i]).kind() == LogosType::Kind::Tuple &&
                            el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                            el.kind() == lir_schema::expr::Code::TupleLit) {
                            lir_view::ETupleLitView itl{el};
                            uint64_t ii = 0;
                            itl.each_elem([&](lir_view::ExprRef iel) {
                                if (ii >= TypeRef(TypeRef(ft).tuple_elems()[i]).tuple_elems().size()) { ++ii; return; }
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (TypeRef(TypeRef(ft).tuple_elems()[i]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(ft).tuple_elems()[i]).tuple_elems()[ii]).kind()))
                                            error(std::format("struct literal '{}' field '{}': tuple element {}: sub-element {}: value {} does not fit in {}",
                                                  sname, fname, i, ii, *v, type_str(TypeRef(TypeRef(ft).tuple_elems()[i]).tuple_elems()[ii])));
                                ++ii;
                            });
                        }
                        ++i;
                    });
                }
            }
        }
    }
    // Handle struct update syntax: Foo { x: 1, ..base }
    // For any field not explicitly set, read it from the base expression.
    if (node.has_key(la::BASE)) {
        auto base_node = map_of(node.get(la::BASE.code));
        auto base_expr = lower_expr(base_node);
        // Sprint 3.4: enforce that `..base` carries the same struct type as
        // the constructor (closes B-li-03 — Foo+..bar silently spread foreign
        // bytes into Bar).
        if (expr_type(base_expr)) {
            TypeRef bt = expr_type(base_expr);
            auto bk = bt.kind();
            bool ok = (bk == LogosType::Kind::Struct || bk == LogosType::Kind::ZonedStruct)
                      && bt.struct_name() == std::string_view(sname);
            if (!ok) {
                error(std::format(
                    "struct literal '{}': '..base' must have type '{}' (got '{}')",
                    sname, sname,
                    (bk == LogosType::Kind::Error) ? "?" : type_str(bt)));
            }
        }
        // Determine base variable name for EVarRef (simple case)
        std::string base_var;
        {
            auto er = expr_ref_of(base_expr);
            if (er.kind() == lir_schema::expr::Code::VarRef)
                base_var = std::string(lir_view::EVarRefView{er}.name());
        }
        for (auto& [fname, inited] : initialized) {
            if (!inited) {
                inited = true;
                auto ft = field_type_of(std::string(sname), fname);
                lir::LExprPtr recv = nullptr;
                if (!base_var.empty()) {
                    recv = builder().var_ref(base_var, expr_type(base_expr));
                } else {
                    // Complex base: re-lower (might evaluate twice, but rare)
                    recv = lower_expr(base_node);
                }
                auto field_val = builder().field_read(std::move(recv), fname, ft ? ft : error_t());
                fields.push_back({fname, std::move(field_val)});
            }
        }
    }

    // §6.1: union literals are partial-by-design — skip the
    // missing-field check (only one field is "active").
    if (!sinfo.is_union) {
        for (auto& [fname, init] : initialized)
            if (!init)
                error(std::format("struct literal '{}': field '{}' not initialized", sname, fname));
    }

    // Move semantics: mark Move-typed field values as consumed.
    for (auto& [fname, fval] : fields) {
        if (fval && is_move_type(expr_type(fval)))
            mark_moved_expr(expr_ref_of(fval));
    }

    std::vector<std::string> ng_lt_args;
    if (hint_struct_type_ && TypeRef(hint_struct_type_).struct_name() == std::string(sname))
        ng_lt_args = TypeRef(hint_struct_type_).lifetime_args();
    // ── SUBSTITUTE AT THE LITERAL (probes ltsubstlit / ltmintsubst) ─────────
    // A struct's own lifetime parameter arrives here UNSUBSTITUTED, and the
    // literal's lifetime args are taken from the HINT — i.e. from the type the
    // context EXPECTS, never from the values actually stored. So
    // `fn mk<'b,'a>(y:&'b i64) -> Holder<'a> { return Holder{v:y}; }` builds
    // `Holder<'a>` out of a `&'b` field and the return comparison is 'a vs 'a.
    // That is why `lifereg_unmentbind` catches this program only when the
    // struct's binder happens to be SPELLED like the fn's (PROBES.md 2026-08-31g,
    // the u7/u8 pair, pinned as fixtures). The fix is substitution: pair each
    // declared field type against the actual field VALUE's type and read the
    // struct's binder off the value's region.
    // LANDED 2026-09-09 (was PROBE ltsubstlit — its only site, retired by this
    // landing). The literal's lifetime args are read off its VALUES.
    if (!sinfo.lifetime_params.empty()) {
        logos::probe::census("subst.structlit.site");
        std::unordered_map<std::string, std::string> flt;
        std::function<void(TypeRef, TypeRef)> walk = [&](TypeRef dt, TypeRef at) {
            if (!dt || !at) return;
            using K = LogosType::Kind;
            auto dk2 = dt.kind();
            if ((dk2 == K::Ref || dk2 == K::MutRef) &&
                (at.kind() == K::Ref || at.kind() == K::MutRef)) {
                std::string d(dt.lifetime()), a(at.lifetime());
                if (!d.empty() && !a.empty() && !flt.count(d)) flt.emplace(d, a);
                walk(dt.pointee(), at.pointee());
                return;
            }
            // Fat-pointer kinds: twin of the arm in structlit_lt_subst_
            // (sema_impl.hpp), first-wins here as every arm of this walk.
            if ((dk2 == K::Slice || dk2 == K::TraitObject || dk2 == K::DstRef) &&
                at.kind() == dk2) {
                std::string d(dt.lifetime()), a(at.lifetime());
                if (!d.empty() && !a.empty() && !flt.count(d)) flt.emplace(d, a);
                if (dk2 == K::Slice) walk(dt.elem(), at.elem());
                else {
                    auto da = dt.type_args(); auto aa = at.type_args();
                    for (size_t i = 0; i < da.size() && i < aa.size(); ++i) walk(da[i], aa[i]);
                }
                return;
            }
            if ((dk2 == K::Struct || dk2 == K::ZonedStruct || dk2 == K::Enum) &&
                at.kind() == dk2) {
                auto dl = dt.lifetime_args(); auto al = at.lifetime_args();
                for (size_t i = 0; i < dl.size() && i < al.size(); ++i)
                    if (!dl[i].empty() && !al[i].empty() && !flt.count(dl[i]))
                        flt.emplace(dl[i], al[i]);
                auto da = dt.type_args(); auto aa = at.type_args();
                for (size_t i = 0; i < da.size() && i < aa.size(); ++i) walk(da[i], aa[i]);
                return;
            }
            if (dk2 == K::Tuple && at.kind() == K::Tuple) {
                auto de = dt.tuple_elems(); auto ae = at.tuple_elems();
                for (size_t i = 0; i < de.size() && i < ae.size(); ++i) walk(de[i], ae[i]);
                return;
            }
        };
        for (auto& f : sinfo.fields)
            for (auto& [fname, fval] : fields)
                if (fval && fname == f.name) { walk(f.type, expr_type(fval)); break; }
        if (!flt.empty()) {
            ng_lt_args.resize(sinfo.lifetime_params.size());
            for (size_t i = 0; i < sinfo.lifetime_params.size(); ++i) {
                auto it = flt.find(sinfo.lifetime_params[i]);
                if (it != flt.end()) {
                    if (ng_lt_args[i] != it->second) logos::probe::census("subst.structlit.differs");
                    ng_lt_args[i] = it->second;
                }
            }
        }
        // A binder the VALUES did not spell stays EMPTY-BUT-PRESENT: "no fact
        // recorded" and "the fact is absent" are different, and only this
        // minting site can tell them apart. A type that never reached a mint
        // keeps zero args and still yields at the comparison.
        logos::probe::census("lit.mint.sized");
        ng_lt_args.assign(sinfo.lifetime_params.size(), std::string{});
        for (size_t i = 0; i < sinfo.lifetime_params.size(); ++i)
            if (auto it = flt.find(sinfo.lifetime_params[i]); it != flt.end())
                ng_lt_args[i] = it->second;
    }
    // B77: verify struct's `where 'a: 'b` against caller's outlives graph.
    check_struct_lit_outlives(std::string(sname),
                              sinfo.lifetime_params,
                              sinfo.lifetime_outlives,
                              ng_lt_args,
                              sinfo.fields,
                              fields);
    LogosTypeBuilder ng_t;
    ng_t.kind = slit_is_zoned
                ? LogosType::Kind::ZonedStruct : LogosType::Kind::Struct;
    ng_t.struct_name   = std::string(sname);
    if (auto rp = resolve_struct_pkg_(sname); !rp.empty()) ng_t.pkg_name = std::move(rp);
    ng_t.lifetime_args = std::move(ng_lt_args);
    TypeRef lit_result_type = pool_->alloc(std::move(ng_t));
    return builder().struct_lit(std::string(sname), std::move(fields), lit_result_type);
}

lir::LExprPtr SemaChecker::lower_index_place(TinyMapView node, bool is_mut) {
    if (code_of(node) != la::INDEX_READ) return nullptr;
    auto recv_node = map_of(node.get(la::RECEIVER.code));
    auto recv = lower_expr(recv_node);
    auto arr_type = expr_type(recv);
    if (TypeRef(arr_type).kind() != LogosType::Kind::Struct) return nullptr;

    auto type_name = concrete_struct_name(arr_type);
    auto base_name = std::string(TypeRef(arr_type).struct_name());
    // For `&mut f[i]` we need IndexMut; for `&f[i]`, Index is enough.
    const char* trait = is_mut ? "IndexMut" : "Index";
    bool has_trait = impls_.count(std::string(trait) + "::" + type_name) ||
                     (!base_name.empty() &&
                      impls_.count(std::string(trait) + "::" + base_name));
    if (!has_trait) {
        // `&mut f[i]` but no IndexMut impl — not a user index-place we can
        // honour. Fall through (generic path will diagnose / copy).
        return nullptr;
    }
    std::string method = is_mut ? "__index_mut" : "__index";
    auto mangled = type_name + method;
    const SemaFuncInfo* fit = nullptr;
    for (auto* c : find_func_candidates(mangled)) {
        if (c->param_types.size() == 2) { fit = c; break; }
    }
    if (!fit) return nullptr;

    lir::LExprPtr idx = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code)))
        : error_expr();
    widen_int_expr(idx, fit->param_types[1], builder());

    // Receiver address: for a plain variable take the REAL slot address
    // (`&mut f`), not a spilled copy — otherwise index_mut writes into a
    // temporary and the mutation is lost. Other receiver shapes fall back to
    // addr_of_temp (best-effort; a place chain like `g.h[i]` keeps the
    // pre-existing behaviour).
    auto self_ref_t = make_ref(is_mut, arr_type);
    lir::LExprPtr recv_ref = nullptr;
    if (code_of(recv_node) == la::VAR_REF) {
        auto var_name = std::string(str_of(recv_node.get(la::NAME.code)));
        recv_ref = builder().addr_of(var_name, self_ref_t);
    } else if (is_ref_like(TypeRef(arr_type).kind())) {
        // Receiver is already a reference/pointer to the struct — pass through.
        recv_ref = std::move(recv);
    } else {
        recv_ref = materialize_recv_ref(std::move(recv), is_mut, self_ref_t);
    }
    std::vector<lir::LExprPtr> args;
    args.push_back(std::move(recv_ref));
    args.push_back(std::move(idx));
    // index_mut returns `&mut Output` / index returns `&Output`; that IS the
    // place reference the caller wants — return it directly (no deref).
    return builder().call(
        fit->symbol_name.empty() ? mangled : fit->symbol_name,
        {}, std::move(args), fit->ret_type);
}

lir::LExprPtr SemaChecker::lower_index_read(TinyMapView node) {
    // Consume the mutable-use context (see `mut_place_ctx_`): an indexed place
    // written or borrowed mutably takes the IndexMut step, and its receiver
    // is itself a place in the same position (`*v[i] = x`, `b[i] = x` on a Box).
    bool mut_ctx = mut_place_ctx_;
    auto recv_node = map_of(node.get(la::RECEIVER.code));
    auto recv = mut_ctx ? lower_mut_place(recv_node) : lower_expr(recv_node);
    mut_place_ctx_ = false;
    auto arr_type = expr_type(recv);

    // Rust autoderef at the index position: peel `&&…` chains with REAL
    // deref-loads until at most one reference remains. A `&&[T; N]` receiver
    // used to fall into the single-level Ref handling, which takes the
    // POINTEE as the element — so `rr[1]` typed as the whole `&[T; N]`.
    while (arr_type &&
           (TypeRef(arr_type).kind() == LogosType::Kind::Ref ||
            TypeRef(arr_type).kind() == LogosType::Kind::MutRef) &&
           TypeRef(arr_type).pointee() &&
           (TypeRef(TypeRef(arr_type).pointee()).kind() == LogosType::Kind::Ref ||
            TypeRef(TypeRef(arr_type).pointee()).kind() == LogosType::Kind::MutRef)) {
        arr_type = TypeRef(arr_type).pointee();
        recv = builder().deref(std::move(recv), arr_type);
    }

    // `&[T]` is a fat-pointer SLICE value (Kind::Slice). A reference TO such a
    // slice (`&s` where `s: &[T]`, or a `&[T;N]` array-ref that decayed to a
    // slice and then got `&`-borrowed) arrives as `Ref→Slice`. The slice-index
    // codegen treats its slice operand as a POINTER into the `{data,len}` pair
    // (it GEPs field 0), and a `Ref→Slice` IS exactly that pointer — so we just
    // RETYPE the receiver to the pointee Slice (NOT a deref-load: loading the
    // 16-byte fat value and then GEP-ing it as a pointer segfaults). Without
    // this the element type resolves to the whole slice → `(&s)[i]` typed `&[T]`.
    if (arr_type &&
        (TypeRef(arr_type).kind() == LogosType::Kind::Ref ||
         TypeRef(arr_type).kind() == LogosType::Kind::MutRef) &&
        TypeRef(arr_type).pointee() &&
        TypeRef(TypeRef(arr_type).pointee()).kind() == LogosType::Kind::Slice) {
        arr_type = TypeRef(arr_type).pointee();
        builder().retype_expr(recv, arr_type);
    }

    // Range index `recv[lo..hi]` / `recv[lo..]` / `recv[..hi]` / `recv[..]` →
    // sub-slice via `slice_get_range` (Rust-parity slicing). Detect the
    // RANGE_EXPR index AST before lowering it as a scalar index.
    if (node.has_key(la::VALUE)) {
        auto idx_node = map_of(node.get(la::VALUE.code));
        if (code_of(idx_node) == la::RANGE_EXPR) {
            // Coerce receiver to `&[T]`: arrays decay to a slice; a `&[T]` /
            // str is used directly. (Vec → `.as_slice()` is a follow-up.)
            TypeRef elem = nullptr;
            auto rk = TypeRef(arr_type).kind();
            if (rk == LogosType::Kind::Slice) {
                elem = TypeRef(arr_type).elem();
            } else if (rk == LogosType::Kind::Array) {
                elem = TypeRef(arr_type).elem();
                // Array value → `&[T;N]` (addr-of) → `&[T]` (slice decay).
                recv = builder().addr_of_temp(std::move(recv), /*is_mut=*/false,
                                              make_ref(false, arr_type));
                try_coerce_array_ref_to_slice(recv, make_slice_type(elem ? elem : i32_t()));
            } else if ((rk == LogosType::Kind::Ref || rk == LogosType::Kind::MutRef) &&
                       TypeRef(arr_type).pointee() &&
                       TypeRef(TypeRef(arr_type).pointee()).kind() == LogosType::Kind::Slice) {
                elem = TypeRef(TypeRef(arr_type).pointee()).elem();
            }
            if (!elem || TypeRef(elem).kind() == LogosType::Kind::Error) {
                error(std::format("range index `[..]` requires a slice or array "
                      "receiver, got {}", type_str(arr_type)));
                return error_expr();
            }
            TypeRef i64t = prim(LogosType::Kind::I64);
            lir::LExprPtr lo = idx_node.has_key(la::LHS)
                ? lower_expr(map_of(idx_node.get(la::LHS.code))) : builder().lit_int(0, i64t);
            widen_int_expr(lo, i64t, builder());
            lir::LExprPtr hi = nullptr;
            if (idx_node.has_key(la::RHS)) {
                hi = lower_expr(map_of(idx_node.get(la::RHS.code)));
                widen_int_expr(hi, i64t, builder());
                bool inclusive = idx_node.has_key(la::INCLUSIVE) &&
                    !idx_node.get(la::INCLUSIVE.code).is_null() &&
                    idx_node.get(la::INCLUSIVE.code).as_value<uint8_t>() != 0;
                if (inclusive)  // ..=hi → hi+1 (clamped by slice_get_range)
                    hi = builder().bin_op("+", std::move(hi), builder().lit_int(1, i64t), i64t);
            } else {
                hi = builder().lit_int(INT64_MAX, i64t);  // open end → clamp to len
            }
            auto cands = find_func_candidates("slice_get_range");
            const SemaFuncInfo* sgr = cands.empty() ? nullptr : cands[0];
            if (!sgr) {
                error("range index: stdlib `slice_get_range` not in scope "
                      "(missing `use logos.lang.slice`)");
                return error_expr();
            }
            TypeRef ret_t = make_slice_type(elem);
            std::vector<lir::LExprPtr> args;
            args.push_back(std::move(recv));
            args.push_back(std::move(lo));
            args.push_back(std::move(hi));
            std::string sym = sgr->symbol_name.empty() ? std::string("slice_get_range")
                                                       : sgr->symbol_name;
            if (!sgr->type_params.empty())
                return finish_generic_call(sym, *sgr, {elem}, std::move(args));
            return builder().call(sym, {}, std::move(args), ret_t);
        }
    }

    lir::LExprPtr idx = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code)))
        : error_expr();

    // User-defined Index dispatch: `a[i]` for struct a where a impls
    // Index<Idx, Out> → `*(a.index(i))`. Tried before the built-in
    // integer-index check so a user impl can accept non-integer keys.
    // Rust autoderef at INDEX position: a struct receiver WITHOUT an Index
    // impl derefs through its Deref impl(s) until an indexable type appears
    // (`w[0]` where W: Deref<Vec<i64>> — adversarial #2 p15). Bounded walk
    // mirrors method-resolution autoderef.
    for (int _ad_step = 0; _ad_step < 4 &&
         TypeRef(arr_type).kind() == LogosType::Kind::Struct; ++_ad_step) {
        {
            auto _tn = concrete_struct_name(arr_type);
            auto _bn = std::string(TypeRef(arr_type).struct_name());
            bool _has_index = impls_.count("Index::" + _tn) ||
                              (!_bn.empty() && impls_.count("Index::" + _bn));
            if (_has_index) break;
        }
        bool deref_only = false;
        auto stepped = emit_generic_deref_call(std::move(recv), /*want_mut=*/mut_ctx,
                                               &deref_only);
        if (!stepped) break;     // recv (raw handle) still valid — no Deref impl
        if (mut_ctx && deref_only) { refuse_deref_only(arr_type); return error_expr(); }
        TypeRef rt2 = expr_type(*stepped);
        if (TypeRef(rt2).kind() == LogosType::Kind::Slice ||
            TypeRef(rt2).kind() == LogosType::Kind::TraitObject) {
            recv = std::move(*stepped);   // fat form IS the value
            arr_type = rt2;
            break;
        }
        TypeRef tgt = TypeRef(rt2).pointee() ? TypeRef(rt2).pointee() : rt2;
        recv = builder().deref(std::move(*stepped), tgt);
        arr_type = expr_type(recv);
    }
    if (TypeRef(arr_type).kind() == LogosType::Kind::Struct) {
        auto type_name = concrete_struct_name(arr_type);
        auto base_name = std::string(TypeRef(arr_type).struct_name());
        bool has_index = impls_.count("Index::" + type_name) ||
                         (!base_name.empty() && impls_.count("Index::" + base_name));
        // In a mutable-use position the step is `index_mut` (IndexMut), and
        // an `Index`-only type is not a writable place (E0594) — the same
        // refusal `lower_place_assign` gives a bare-variable receiver.
        const char* itr = mut_ctx ? "IndexMut" : "Index";
        bool has_trait = impls_.count(std::string(itr) + "::" + type_name) ||
                         (!base_name.empty() && impls_.count(std::string(itr) + "::" + base_name));
        if (has_index && mut_ctx && !has_trait) {
            error(std::format("cannot assign to index of '{}': type '{}' implements "
                              "`Index` but not `IndexMut`", type_str(arr_type),
                              type_name.empty() ? base_name : type_name));
            return error_expr();
        }
        if (has_index) {
            auto mangled = type_name + (mut_ctx ? "__index_mut" : "__index");
            auto ref_t = make_ref(mut_ctx, arr_type);
            // Pick the unique 2-param candidate (recv + idx). Widen
            // integer-literal idx to the formal type so `m[3]`-style
            // literal indexes match.
            const SemaFuncInfo* fit = nullptr;
            for (auto* c : find_func_candidates(mangled)) {
                if (c->param_types.size() == 2) { fit = c; break; }
            }
            if (fit) {
                widen_int_expr(idx, fit->param_types[1], builder());
                auto recv_ref = materialize_recv_ref(std::move(recv), mut_ctx, ref_t);
                std::vector<lir::LExprPtr> args;
                args.push_back(std::move(recv_ref));
                args.push_back(std::move(idx));
                auto call_e = builder().call(
                    fit->symbol_name.empty() ? mangled : fit->symbol_name,
                    {}, std::move(args), fit->ret_type);
                auto pointee = TypeRef(expr_type(call_e)).pointee()
                    ? TypeRef(expr_type(call_e)).pointee()
                    : error_t();
                return builder().deref(std::move(call_e), pointee);
            }
            // Generic-struct Index impl (`impl<T> Index for Vec<T>`): the
            // concrete `Vec$G1$i64__index` symbol doesn't exist at sema (only
            // the template), so the concrete-symbol path above misses. Route
            // `v[i]` → `*v.index(i)` through the normal method-call machinery
            // (which dispatches generic-struct methods + instantiates at mono).
            // Output = the impl's `Index<Idx, Output>` 2nd trait-arg, with the
            // struct's type-args substituted for the impl's type params.
            const SemaImplInfo* ii = nullptr;
            if (auto it = impls_.find(std::string(itr) + "::" + type_name); it != impls_.end()) ii = &it->second;
            else if (auto it2 = impls_.find(std::string(itr) + "::" + base_name); it2 != impls_.end()) ii = &it2->second;
            if (ii && ii->trait_type_args.size() >= 2) {
                SemaSubst subst;
                if (ii->target_typeref) {
                    auto pat = TypeRef(ii->target_typeref).type_args();
                    auto cur = TypeRef(arr_type).type_args();
                    for (size_t k = 0; k < pat.size() && k < cur.size(); ++k)
                        if (pat[k] && TypeRef(pat[k]).kind() == LogosType::Kind::TypeVar)
                            subst[std::string(TypeRef(pat[k]).type_var_name())] = cur[k];
                }
                TypeRef idx_t = subst_type_sema(ii->trait_type_args[0], subst);
                TypeRef out_t = subst_type_sema(ii->trait_type_args[1], subst);
                if (idx_t && TypeRef(idx_t).kind() != LogosType::Kind::TypeVar)
                    widen_int_expr(idx, idx_t, builder());
                lir::EMethodCall mc;
                mc.receiver = materialize_recv_ref(std::move(recv), mut_ctx, ref_t);
                mc.method = mut_ctx ? "index_mut" : "index";
                mc.args.push_back(std::move(idx));
                mc.vtable_index = -1;
                mc.resolved_type = "";
                auto call_e = builder().method_call_v(std::move(mc), make_ref(mut_ctx, out_t));
                return builder().deref(std::move(call_e), out_t);
            }
        }
    }

    if (!is_integer(expr_type(idx)))
        error(std::format("array index must be integer, got {}", type_str(expr_type(idx))));

    // Slice indexing: s[i] → ESliceIndex
    if (TypeRef(arr_type).kind() == LogosType::Kind::Slice) {
        auto elem = TypeRef(arr_type).elem() ? TypeRef(arr_type).elem() : error_t();
        return builder().slice_index(std::move(recv), std::move(idx), elem);
    }

    if (TypeRef(arr_type).kind() != LogosType::Kind::Array &&
        TypeRef(arr_type).kind() != LogosType::Kind::Ptr &&
        TypeRef(arr_type).kind() != LogosType::Kind::Ref &&
        TypeRef(arr_type).kind() != LogosType::Kind::MutRef &&
        TypeRef(arr_type).kind() != LogosType::Kind::Error) {
        error(std::format("index read: receiver is not an array, slice, or pointer (got {})",
              type_str(arr_type)));
    }
    if (TypeRef(arr_type).kind() == LogosType::Kind::Ptr && !inside_unsafe_) {
        error("index read through raw pointer requires unsafe context");
    }

    TypeRef elem = error_t();
    if (TypeRef(arr_type).kind() == LogosType::Kind::Array && TypeRef(arr_type).elem())  elem = TypeRef(arr_type).elem();
    if ((TypeRef(arr_type).kind() == LogosType::Kind::Ptr ||
         TypeRef(arr_type).kind() == LogosType::Kind::Ref ||
         TypeRef(arr_type).kind() == LogosType::Kind::MutRef) && TypeRef(arr_type).pointee()) {
        // For `&[T; N]` / `&mut [T; N]` / `*const [T; N]`, indexing through
        // the ref auto-derefs and yields the element type. Without this
        // step `a[0]` for `a: &[i32; N]` returns the whole array (with N
        // unresolved → "expected i32, got [i32; 0]").
        TypeRef pointee = TypeRef(arr_type).pointee();
        if (pointee.kind() == LogosType::Kind::Array && pointee.elem())
            elem = pointee.elem();
        else
            elem = pointee;
    }

    return builder().index_read(std::move(recv), std::move(idx), elem);
}

lir::LExprPtr SemaChecker::lower_arr_lit(TinyMapView node) {
    // Empty array literal `[]` / `&[]`. The element type is unknown from the
    // literal itself, but a `[T; N]` / `[T]` / `&[T]` annotation (or return
    // type) supplies it via hint_arr_elem_type_. Build an empty `[T; 0]` so a
    // borrow coerces to an empty slice `&[T]` (Rust `let s: &[u64] = &[];`).
    // Without a hint there is genuinely nothing to infer → keep the warning.
    bool no_items = !node.has_key(la::ITEMS);
    if (!no_items) {
        auto items0 = arr_of(node.get(la::ITEMS.code));
        no_items = (items0.size() == 0);
    }
    if (no_items) {
        if (hint_arr_elem_type_ &&
            TypeRef(hint_arr_elem_type_).kind() != LogosType::Kind::Error) {
            auto ty = make_array(hint_arr_elem_type_, 0);
            return builder().arr_lit(std::vector<lir::LExprPtr>{}, ty);
        }
        warn("empty array literal: element type unknown");
        return error_expr();
    }
    auto items = arr_of(node.get(la::ITEMS.code));
    std::vector<lir::LExprPtr> elems;
    for (uint64_t i = 0; i < items.size(); ++i)
        elems.push_back(lower_expr(map_of(items.get(i))));

    TypeRef elem_type = expr_type(elems[0]);
    // T0-5: a CONCRETE scalar element hint (a `&[i64]` formal / annotation,
    // via hint_arr_elem_type_) retypes an all-literal array's elements up
    // front. Slices alias raw memory, so the buffer must be BUILT at the
    // annotated width — the old flow let the lits default to i32 and the
    // permissive slice coercion read garbage at i64 stride.
    if (hint_arr_elem_type_) {
        auto hk = TypeRef(hint_arr_elem_type_).kind();
        bool hint_int = is_integer_kind(hk) &&
                        hk != LogosType::Kind::IntLit &&
                        hk != LogosType::Kind::Enum;
        bool hint_float = hk == LogosType::Kind::F32 ||
                          hk == LogosType::Kind::F64;
        if (hint_int || hint_float) {
            bool adoptable = true;
            size_t ei = 0;
            for (auto& e : elems) {
                auto k = TypeRef(expr_type(e)).kind();
                ++ei;
                if (k == LogosType::Kind::Error) continue;
                if (types_equal(expr_type(e), hint_arr_elem_type_)) continue;
                if (hint_int && k == LogosType::Kind::IntLit) {
                    if (auto v = get_intlit_value(e)) {
                        if (intlit_fits(*v, hk)) continue;
                        // Out-of-range literal for the annotated width is an
                        // error, not a silent fall-back to the i32 default
                        // (which the slice-aliasing check would then reject
                        // with a misleading type-mismatch).
                        error(std::format(
                            "array literal: element {}: value {} does not fit in {}",
                            ei - 1, *v, type_str(hint_arr_elem_type_)));
                        continue;
                    }
                }
                if (hint_float && k == LogosType::Kind::FloatLit) continue;
                adoptable = false;
                break;
            }
            if (adoptable) {
                for (auto& e : elems) {
                    auto k = TypeRef(expr_type(e)).kind();
                    if (k == LogosType::Kind::IntLit ||
                        k == LogosType::Kind::FloatLit)
                        builder().retype_expr(e, hint_arr_elem_type_);
                }
                elem_type = hint_arr_elem_type_;
            }
        }
    }
    // logos-core 1.4: a `[fn(...) -> R; N]` annotation lets a heterogeneous
    // array of distinct FnItems (each `fn-name` bare-ref) unify to a common
    // FnPtr. Each FnItem → FnPtr coerces via types_compatible; adopt the
    // hint as the element type so the homogeneity check below sees FnPtr,
    // not the per-element FnItem.
    bool fnptr_elem_hint = false;
    if (hint_arr_elem_type_ &&
        TypeRef(hint_arr_elem_type_).kind() == LogosType::Kind::FnPtr) {
        bool all_coerce = true;
        for (auto& e : elems) {
            TypeRef et = expr_type(e);
            if (TypeRef(et).kind() == LogosType::Kind::Error) continue;
            if (types_compatible(et, hint_arr_elem_type_)) continue;
            all_coerce = false; break;
        }
        if (all_coerce) {
            elem_type = hint_arr_elem_type_;
            fnptr_elem_hint = true;
            for (auto& e : elems) {
                if (!e || TypeRef(expr_type(e)).kind() == LogosType::Kind::Error)
                    continue;
                if (types_equal(expr_type(e), hint_arr_elem_type_)) continue;
                e = builder().cast(std::move(e), hint_arr_elem_type_);
            }
        }
    }
    // `[&arr3, &arr5]` under a `[&[T]; N]` annotation: each element decays to
    // the hinted slice, exactly as it would at any other expected-type
    // position. Without this the elements keep their per-length types and the
    // literal is heterogeneous by construction.
    if (hint_arr_elem_type_ &&
        (TypeRef(hint_arr_elem_type_).kind() == LogosType::Kind::Slice ||
         TypeRef(hint_arr_elem_type_).kind() == LogosType::Kind::UnsizedSlice)) {
        bool any = false;
        for (auto& e : elems) {
            if (!e || TypeRef(expr_type(e)).kind() == LogosType::Kind::Error)
                continue;
            if (try_coerce_array_ref_to_slice(e, hint_arr_elem_type_)) any = true;
        }
        // elem_type was derived from element 0 BEFORE the decay; recompute it
        // or the literal keeps the pre-decay per-length type and every other
        // element mismatches against it.
        if (any) elem_type = hint_arr_elem_type_;
    }
    // g6b: a `[&dyn Trait; N]` annotation lets a HETEROGENEOUS array of distinct
    // `&Concrete` refs unify to `&dyn Trait`. When the expected element type is
    // known and every element coerces to it (with at least one needing the
    // `&Concrete → &dyn` unsize), adopt the hint as the element type and skip
    // the homogeneity checks below — codegen builds each fat pointer per-element.
    bool dyn_elem_hint = false;
    if (hint_arr_elem_type_ &&
        TypeRef(hint_arr_elem_type_).kind() != LogosType::Kind::Error) {
        TypeRef he = hint_arr_elem_type_;
        // ref_arg_satisfies_dyn wants the bare TraitObject; `he` is the ref form
        // `&dyn Trait` (Ref→TraitObject) when from a `[&dyn Trait; N]` annotation.
        TypeRef he_dyn = he;
        if ((TypeRef(he).kind() == LogosType::Kind::Ref ||
             TypeRef(he).kind() == LogosType::Kind::MutRef) &&
            TypeRef(he).pointee() &&
            TypeRef(TypeRef(he).pointee()).kind() == LogosType::Kind::TraitObject)
            he_dyn = TypeRef(he).pointee();
        // A `[&dyn Trait; N]` hint (element type is a TraitObject) wants every
        // element coerced to the fat `&dyn` pointer. Engage when each element is
        // compatible with, or unsize-coercible to, that dyn element type.
        bool he_is_dyn = TypeRef(he_dyn).kind() == LogosType::Kind::TraitObject;
        if (he_is_dyn) {
            bool all_coerce = true;
            for (auto& e : elems) {
                TypeRef et = expr_type(e);
                if (TypeRef(et).kind() == LogosType::Kind::Error) continue;
                if (types_compatible(et, he) || ref_arg_satisfies_dyn(et, he_dyn)) continue;
                all_coerce = false; break;
            }
            if (all_coerce) {
                elem_type = he;
                dyn_elem_hint = true;
                // Wrap each not-already-`&dyn` element in an explicit
                // dyn-coercion cast so codegen builds the fat pointer (vtable)
                // per element AND mono's scan collects the concrete coercion
                // target (so its blanket method instantiates). Adopting the hint
                // TYPE alone leaves a thin `&Concrete` in the `&dyn` slot —
                // reading the (absent) vtable SIGSEGVs. The explicit-cast form
                // (`&x as &dyn`) already produced this ECast; the implicit form
                // (bare `&x` under a `[&dyn; N]` hint) did not.
                for (auto& e : elems) {
                    if (!e || TypeRef(expr_type(e)).kind() == LogosType::Kind::Error)
                        continue;
                    if (types_compatible(expr_type(e), he)) continue;  // already &dyn
                    e = builder().cast(std::move(e), he);
                }
            }
        }
    }
    for (uint64_t i = 1; !dyn_elem_hint && !fnptr_elem_hint && i < elems.size(); ++i) {
        auto t = expr_type(elems[i]);
        if (TypeRef(t).kind() != LogosType::Kind::Error && TypeRef(elem_type).kind() != LogosType::Kind::Error) {
            if (!types_compatible(t, elem_type) && !types_compatible(elem_type, t)) {
                { auto [es, gs] = type_str_pair(t, elem_type);
                  error(std::format("array literal: element {} has type {}, expected {}",
                      i, es, gs)); }
            } else {
                // If the concrete element type is narrow and this element is IntLit, check range.
                if (TypeRef(t).kind() == LogosType::Kind::IntLit &&
                    TypeRef(elem_type).kind() != LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(elems[i]))
                        if (!intlit_fits(*v, TypeRef(elem_type).kind()))
                            error(std::format("array literal: element {}: value {} does not fit in {}",
                                  i, *v, type_str(elem_type)));
                // Check array literal elements against narrow nested array element types.
                if (TypeRef(elem_type).kind() == LogosType::Kind::Array && TypeRef(elem_type).elem() &&
                    TypeRef(t).kind() == LogosType::Kind::Array) {
                    auto vr = expr_ref_of(elems[i]);
                    if (vr.kind() == lir_schema::expr::Code::ArrLit) {
                        lir_view::EArrLitView al{vr};
                        for (uint64_t ei = 0; ei < al.count(); ++ei) {
                            auto el = al.elem(ei);
                            if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(el))
                                    if (!intlit_fits(*v, TypeRef(elem_type).elem().kind()))
                                        error(std::format("array literal: element {}: sub-element {}: value {} does not fit in {}",
                                              i, ei, *v, type_str(TypeRef(elem_type).elem())));
                        }
                    }
                }
                // Check tuple literal elements against narrow nested tuple element types.
                if (TypeRef(elem_type).kind() == LogosType::Kind::Tuple && TypeRef(t).kind() == LogosType::Kind::Tuple) {
                    auto vr = expr_ref_of(elems[i]);
                    if (vr.kind() == lir_schema::expr::Code::TupleLit) {
                        lir_view::ETupleLitView tl{vr};
                        uint64_t ei = 0;
                        tl.each_elem([&](lir_view::ExprRef el) {
                            if (ei >= TypeRef(elem_type).tuple_elems().size()) { ++ei; return; }
                            if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(el))
                                    if (TypeRef(elem_type).tuple_elems()[ei] && !intlit_fits(*v, TypeRef(TypeRef(elem_type).tuple_elems()[ei]).kind()))
                                        error(std::format("array literal: element {}: tuple element {}: value {} does not fit in {}",
                                              i, ei, *v, type_str(TypeRef(elem_type).tuple_elems()[ei])));
                            if (TypeRef(elem_type).tuple_elems()[ei] && TypeRef(TypeRef(elem_type).tuple_elems()[ei]).kind() == LogosType::Kind::Array &&
                                TypeRef(TypeRef(elem_type).tuple_elems()[ei]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                                el.kind() == lir_schema::expr::Code::ArrLit) {
                                lir_view::EArrLitView ial{el};
                                for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                                    auto iel = ial.elem(ii);
                                    if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(iel))
                                            if (!intlit_fits(*v, TypeRef(TypeRef(elem_type).tuple_elems()[ei]).elem().kind()))
                                                error(std::format("array literal: element {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                      i, ei, ii, *v, type_str(TypeRef(TypeRef(elem_type).tuple_elems()[ei]).elem())));
                                }
                            }
                            if (TypeRef(elem_type).tuple_elems()[ei] && TypeRef(TypeRef(elem_type).tuple_elems()[ei]).kind() == LogosType::Kind::Tuple &&
                                el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                                el.kind() == lir_schema::expr::Code::TupleLit) {
                                lir_view::ETupleLitView itl{el};
                                uint64_t ii = 0;
                                itl.each_elem([&](lir_view::ExprRef iel) {
                                    if (ii >= TypeRef(TypeRef(elem_type).tuple_elems()[ei]).tuple_elems().size()) { ++ii; return; }
                                    if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(iel))
                                            if (TypeRef(TypeRef(elem_type).tuple_elems()[ei]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(elem_type).tuple_elems()[ei]).tuple_elems()[ii]).kind()))
                                                error(std::format("array literal: element {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                      i, ei, ii, *v, type_str(TypeRef(TypeRef(elem_type).tuple_elems()[ei]).tuple_elems()[ii])));
                                    ++ii;
                                });
                            }
                            ++ei;
                        });
                    }
                }
                elem_type = unify_numeric(elem_type, t);
            }
        }
    }
    // Element 0 retroactive check: the loop above only checks elements 1+.
    // If a later element has a concrete narrow type, element 0 (which set
    // elem_type initially) was never range-checked against it.
    // Find the first concrete anchor from elements 1+ and check element 0.
    if (!dyn_elem_hint && elems.size() > 1) {
        // Locate the first element whose type is concrete (not purely IntLit-typed).
        TypeRef anchor = nullptr;
        for (size_t i = 1; i < elems.size() && !anchor; ++i) {
            TypeRef ti = expr_type(elems[i]);
            if (TypeRef(ti).kind() != LogosType::Kind::IntLit &&
                !(TypeRef(ti).kind() == LogosType::Kind::Array && TypeRef(ti).elem() &&
                  TypeRef(ti).elem().kind() == LogosType::Kind::IntLit))
                anchor = ti;
        }
        if (anchor) {
            auto e = elems[0];
            auto t0 = expr_type(elems[0]);
            // Scalar IntLit at element 0.
            if (TypeRef(t0).kind() == LogosType::Kind::IntLit)
                if (auto v = get_intlit_value(e))
                    if (!intlit_fits(*v, TypeRef(anchor).kind()))
                        error(std::format("array literal: element 0: value {} does not fit in {}",
                              *v, type_str(anchor)));
            // Array literal at element 0 (e.g. [[1,200,3], concrete_arr]).
            if (TypeRef(anchor).kind() == LogosType::Kind::Array && TypeRef(anchor).elem() &&
                TypeRef(t0).kind() == LogosType::Kind::Array) {
                auto vr = expr_ref_of(e);
                if (vr.kind() == lir_schema::expr::Code::ArrLit) {
                    lir_view::EArrLitView al{vr};
                    for (uint64_t ei = 0; ei < al.count(); ++ei) {
                        auto el = al.elem(ei);
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (!intlit_fits(*v, TypeRef(anchor).elem().kind()))
                                    error(std::format("array literal: element 0: sub-element {}: value {} does not fit in {}",
                                          ei, *v, type_str(TypeRef(anchor).elem())));
                    }
                }
            }
            // Tuple literal at element 0 (tuple elements, including nested array/tuple).
            if (TypeRef(anchor).kind() == LogosType::Kind::Tuple && TypeRef(t0).kind() == LogosType::Kind::Tuple) {
                auto vr = expr_ref_of(e);
                if (vr.kind() == lir_schema::expr::Code::TupleLit) {
                    lir_view::ETupleLitView tl{vr};
                    uint64_t ei = 0;
                    tl.each_elem([&](lir_view::ExprRef el) {
                        if (ei >= TypeRef(anchor).tuple_elems().size()) { ++ei; return; }
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (TypeRef(anchor).tuple_elems()[ei] && !intlit_fits(*v, TypeRef(TypeRef(anchor).tuple_elems()[ei]).kind()))
                                    error(std::format("array literal: element 0: tuple element {}: value {} does not fit in {}",
                                          ei, *v, type_str(TypeRef(anchor).tuple_elems()[ei])));
                        if (TypeRef(anchor).tuple_elems()[ei] && TypeRef(TypeRef(anchor).tuple_elems()[ei]).kind() == LogosType::Kind::Array &&
                            TypeRef(TypeRef(anchor).tuple_elems()[ei]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                            el.kind() == lir_schema::expr::Code::ArrLit) {
                            lir_view::EArrLitView ial{el};
                            for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                                auto iel = ial.elem(ii);
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (!intlit_fits(*v, TypeRef(TypeRef(anchor).tuple_elems()[ei]).elem().kind()))
                                            error(std::format("array literal: element 0: tuple element {}: array element {}: value {} does not fit in {}",
                                                  ei, ii, *v, type_str(TypeRef(TypeRef(anchor).tuple_elems()[ei]).elem())));
                            }
                        }
                        if (TypeRef(anchor).tuple_elems()[ei] && TypeRef(TypeRef(anchor).tuple_elems()[ei]).kind() == LogosType::Kind::Tuple &&
                            el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                            el.kind() == lir_schema::expr::Code::TupleLit) {
                            lir_view::ETupleLitView itl{el};
                            uint64_t ii = 0;
                            itl.each_elem([&](lir_view::ExprRef iel) {
                                if (ii >= TypeRef(TypeRef(anchor).tuple_elems()[ei]).tuple_elems().size()) { ++ii; return; }
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (TypeRef(TypeRef(anchor).tuple_elems()[ei]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(anchor).tuple_elems()[ei]).tuple_elems()[ii]).kind()))
                                            error(std::format("array literal: element 0: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                  ei, ii, *v, type_str(TypeRef(TypeRef(anchor).tuple_elems()[ei]).tuple_elems()[ii])));
                                ++ii;
                            });
                        }
                        ++ei;
                    });
                }
            }
        }
    }
    // For IntLit element type: upgrade to i64 if any value overflows i32.
    // Keep IntLit (don't collapse to i32) so that annotation-based coercion
    // ([i64; N] = [1, 2, 3]) can use types_compatible([IntLit;N], [i64;N]) → true.
    if (TypeRef(elem_type).kind() == LogosType::Kind::IntLit) {
        bool needs_i64 = false;
        for (const auto& elem : elems) {
            if (auto v = get_intlit_value(elem))
                if (*v > (int64_t)INT32_MAX || *v < (int64_t)INT32_MIN)
                    { needs_i64 = true; break; }
        }
        if (needs_i64) elem_type = prim(LogosType::Kind::I64);
        // else: leave as IntLit — mlir_gen will see the annotation type
    }

    // Const-pack expansion: `[N...]` over a `<const N...: T>` pack. Build
    // `[T; sizeof...(N)]` symbolic-length array; mono will replace the single
    // PackExpand element with one lit_int per pack member.
    if (elems.size() == 1 &&
        expr_ref_of(elems[0]).kind() == lir_schema::expr::Code::PackExpand &&
        TypeRef(elem_type).kind() == LogosType::Kind::ConstVar) {
        std::string pack_name(TypeRef(elem_type).type_var_name());
        TypeRef under = TypeRef(elem_type).pointee();
        if (!under) under = prim(LogosType::Kind::I64);
        LogosTypeBuilder ab; ab.kind = LogosType::Kind::Array;
        ab.elem = under;
        ab.arr_size = 0;
        ab.arr_size_var = std::string(ARR_LEN_PACK_PFX) + pack_name;
        TypeRef arr_t = pool_->alloc(std::move(ab));
        return builder().arr_lit(std::move(elems), arr_t);
    }

    auto ty = make_array(elem_type, elems.size());
    return builder().arr_lit(std::move(elems), ty);
}

// List comprehension:  [elem_expr for x in iter_expr (if guard)?]
// Desugars to a block expression that creates a Vec<T>, iterates over
// iter_expr, optionally filters by guard, and pushes elem_expr into the Vec.
// Requires `use logos.mem.collections.vec;` in scope.
// Iterator support: array / slice (via SForEach); generic iterator path
// (types with .next() returning Option<T>) is deferred.
lir::LExprPtr SemaChecker::lower_list_comp(TinyMapView node) {
    auto var_name = str_of(node.get(la::NAME.code));

    lir::LExprPtr iter = node.has_key(la::ITER)
        ? lower_expr(map_of(node.get(la::ITER.code))) : error_expr();
    TypeRef iter_type = expr_type(iter);

    // Only array/slice iteration supported for now.
    TypeRef elem_type = nullptr;
    int64_t arr_size = 0;
    bool is_slice = false;
    if (TypeRef(iter_type).kind() == LogosType::Kind::Array) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
        arr_size  = (int64_t)TypeRef(iter_type).arr_size();
    } else if (TypeRef(iter_type).kind() == LogosType::Kind::Slice) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
        is_slice  = true;
    } else {
        error(std::format(
            "list comprehension: only array/slice iteration supported (got {})",
            type_str(iter_type)));
        return error_expr();
    }

    // Require Vec<T> available (via `use std.vec`).
    {
        auto [vpkg, vsi] = find_struct_by_name("Vec");
        if (!vsi) {
            error("list comprehension requires `use logos.mem.collections.vec;`");
            return error_expr();
        }
    }
    auto* vec_new_fi = find_generic_func("vec_new");
    if (!vec_new_fi) {
        error("list comprehension: vec_new not found; add `use logos.mem.collections.vec;`");
        return error_expr();
    }

    TypeRef vec_t = make_synth_generic_struct("Vec", {elem_type});

    std::string vec_var = "__lc_v_" + std::to_string(tmp_var_count_++);

    // SLet: let mut vec_var: Vec<T> = vec_new::<T>();
    // Use symbol_name (may include __g__... suffix for method-level generics).
    std::string vec_new_sym = vec_new_fi->symbol_name.empty() ? "vec_new"
                                                              : vec_new_fi->symbol_name;
    auto call_new = builder().call(vec_new_sym, {elem_type}, {}, vec_t);
    lir::SLet let_v;
    let_v.name   = vec_var;
    let_v.type   = vec_t;
    let_v.is_mut = true;
    let_v.value  = std::move(call_new);

    // Lower VALUE + optional GUARD with var_name in scope.
    push_scope();
    define(vec_var, vec_t, true);
    define(std::string(var_name), elem_type, false);
    auto elem_expr = lower_expr(map_of(node.get(la::VALUE.code)));
    lir::LExprPtr guard_expr = nullptr;
    if (node.has_key(la::GUARD))
        guard_expr = lower_expr(map_of(node.get(la::GUARD.code)));
    pop_scope();

    // Call Vec::push(&mut vec_var, elem) as a direct ECall.
    // Emit with callee "Vec__push" and type_args=[elem_type]; mono_clone will
    // rewrite to the struct-specialized name (e.g. Vec$G1$i32__push).
    auto recv = builder().addr_of(vec_var, make_ptr(true, vec_t));
    std::vector<lir::LExprPtr> push_args;
    push_args.push_back(std::move(recv));
    push_args.push_back(std::move(elem_expr));
    auto push_call = builder().call("Vec__push", {elem_type}, std::move(push_args), void_t());

    lir::SExprStmt push_stmt;
    push_stmt.expr = std::move(push_call);

    std::vector<lir_view::StmtRef> loop_body;
    if (guard_expr) {
        lir::SIf sif;
        sif.cond = std::move(guard_expr);
        std::vector<lir_view::StmtRef> then_blk;
        then_blk.push_back(make_stmt_emit(node_line_, std::move(push_stmt)));
        sif.then_ = lir_mirror_block(*cur_prog_, then_blk);
        loop_body.push_back(make_stmt_emit(node_line_, std::move(sif)));
    } else {
        loop_body.push_back(make_stmt_emit(node_line_, std::move(push_stmt)));
    }

    lir::SForEach sfe;
    sfe.var       = std::string(var_name);
    sfe.iter      = std::move(iter);
    sfe.elem_type = elem_type;
    sfe.arr_size  = arr_size;
    sfe.is_slice  = is_slice;
    sfe.body      = lir_mirror_block(*cur_prog_, loop_body);

    std::vector<lir_view::StmtRef> outer;
    outer.push_back(make_stmt_emit(node_line_, std::move(let_v)));
    outer.push_back(make_stmt_emit(node_line_, std::move(sfe)));

    auto result = builder().var_ref(vec_var, vec_t);
    return builder().block_expr(lir_mirror_block(*cur_prog_, outer), std::move(result), vec_t);
}

// Map comprehension:  {kexpr: vexpr for x in iter_expr (if guard)?}
// Desugars to a block that creates a HashMap<K,V>, iterates over iter_expr,
// optionally filters by guard, and inserts (kexpr, vexpr) pairs.
// Requires `use logos.mem.collections.hashmap;` in scope.
lir::LExprPtr SemaChecker::lower_map_comp(TinyMapView node) {
    auto var_name = str_of(node.get(la::NAME.code));

    lir::LExprPtr iter = node.has_key(la::ITER)
        ? lower_expr(map_of(node.get(la::ITER.code))) : error_expr();
    TypeRef iter_type = expr_type(iter);

    TypeRef elem_type = nullptr;
    int64_t arr_size = 0;
    bool is_slice = false;
    if (TypeRef(iter_type).kind() == LogosType::Kind::Array) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
        arr_size  = (int64_t)TypeRef(iter_type).arr_size();
    } else if (TypeRef(iter_type).kind() == LogosType::Kind::Slice) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
        is_slice  = true;
    } else {
        error(std::format(
            "map comprehension: only array/slice iteration supported (got {})",
            type_str(iter_type)));
        return error_expr();
    }

    {
        auto [hmpkg, hmsi] = find_struct_by_name("HashMap");
        if (!hmsi) {
            error("map comprehension requires `use logos.mem.collections.hashmap;`");
            return error_expr();
        }
    }
    auto* hm_new_fi = find_generic_func("hashmap_new");
    if (!hm_new_fi) {
        error("map comprehension: hashmap_new not found; add `use logos.mem.collections.hashmap;`");
        return error_expr();
    }

    std::string hm_var = "__mc_m_" + std::to_string(tmp_var_count_++);

    push_scope();
    define(std::string(var_name), elem_type, false);
    auto key_expr_body = lower_expr(map_of(node.get(la::KEY.code)));
    auto val_expr_body = lower_expr(map_of(node.get(la::VALUE.code)));
    lir::LExprPtr guard_body = nullptr;
    if (node.has_key(la::GUARD))
        guard_body = lower_expr(map_of(node.get(la::GUARD.code)));
    pop_scope();

    TypeRef k_type = expr_type(key_expr_body);
    TypeRef v_type = expr_type(val_expr_body);
    TypeRef hm_t = make_generic_struct("HashMap", {k_type, v_type});

    std::string hm_new_sym = hm_new_fi->symbol_name.empty() ? "hashmap_new"
                                                            : hm_new_fi->symbol_name;
    auto call_new = builder().call(hm_new_sym, {k_type, v_type}, {}, hm_t);
    lir::SLet let_m;
    let_m.name   = hm_var;
    let_m.type   = hm_t;
    let_m.is_mut = true;
    let_m.value  = std::move(call_new);

    // HashMap::insert(&mut hm, key, val) — unsafe method, emitted as direct ECall
    // "HashMap__insert" so mono_clone rewrites to HashMap$G1$..$G2$..__insert.
    auto recv = builder().addr_of(hm_var, make_ptr(true, hm_t));
    std::vector<lir::LExprPtr> ins_args;
    ins_args.push_back(std::move(recv));
    ins_args.push_back(std::move(key_expr_body));
    ins_args.push_back(std::move(val_expr_body));
    auto ins_call = builder().call("HashMap__insert", {k_type, v_type}, std::move(ins_args), void_t());

    lir::SExprStmt ins_stmt;
    ins_stmt.expr = std::move(ins_call);

    std::vector<lir_view::StmtRef> loop_body;
    if (guard_body) {
        lir::SIf sif;
        sif.cond = std::move(guard_body);
        std::vector<lir_view::StmtRef> then_blk;
        then_blk.push_back(make_stmt_emit(node_line_, std::move(ins_stmt)));
        sif.then_ = lir_mirror_block(*cur_prog_, then_blk);
        loop_body.push_back(make_stmt_emit(node_line_, std::move(sif)));
    } else {
        loop_body.push_back(make_stmt_emit(node_line_, std::move(ins_stmt)));
    }

    lir::SForEach sfe;
    sfe.var       = std::string(var_name);
    sfe.iter      = std::move(iter);
    sfe.elem_type = elem_type;
    sfe.arr_size  = arr_size;
    sfe.is_slice  = is_slice;
    sfe.body      = lir_mirror_block(*cur_prog_, loop_body);

    std::vector<lir_view::StmtRef> outer;
    outer.push_back(make_stmt_emit(node_line_, std::move(let_m)));
    outer.push_back(make_stmt_emit(node_line_, std::move(sfe)));

    auto result = builder().var_ref(hm_var, hm_t);
    return builder().block_expr(lir_mirror_block(*cur_prog_, outer), std::move(result), hm_t);
}

// Writ list comprehension:  @[expr for x in iter_expr (if guard)?]
// Desugars to a block that builds a Writ whose root is an
// ObjectArray of AnyVals, iterating over iter_expr and optionally
// filtering by guard.  Element expression must evaluate to AnyVal
// (user coerces scalars explicitly via AnyVal::embed_i24 etc.).
// Requires `use logos.mem.writ.ctr;` in scope.
lir::LExprPtr SemaChecker::lower_writ_list_comp(TinyMapView node) {
    auto var_name = str_of(node.get(la::NAME.code));

    lir::LExprPtr iter = node.has_key(la::ITER)
        ? lower_expr(map_of(node.get(la::ITER.code))) : error_expr();
    TypeRef iter_type = expr_type(iter);

    // Short-circuit on upstream error to avoid cascading diagnostics.
    if (TypeRef(iter_type).kind() == LogosType::Kind::Error)
        return error_expr();

    TypeRef elem_type = nullptr;
    int64_t arr_size = 0;
    bool is_slice = false;
    if (TypeRef(iter_type).kind() == LogosType::Kind::Array) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
        arr_size  = (int64_t)TypeRef(iter_type).arr_size();
    } else if (TypeRef(iter_type).kind() == LogosType::Kind::Slice) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
        is_slice  = true;
    } else {
        error(std::format(
            "writ list comprehension: only array/slice iteration supported (got {})",
            type_str(iter_type)));
        return error_expr();
    }

    // writ builder: yields Rc<Writ> (see lang.writ.comp_builder).
    auto new_cands  = find_func_candidates("writ_list_comp_new");
    auto push_cands = find_func_candidates("writ_list_comp_push");
    const SemaFuncInfo* new_fi  = nullptr;
    const SemaFuncInfo* push_fi = nullptr;
    for (auto* fi : new_cands)  if (fi->param_types.size() == 1) { new_fi  = fi; break; }
    for (auto* fi : push_cands) if (fi->param_types.size() == 2) { push_fi = fi; break; }
    if (!new_fi || !push_fi) {
        error("writ list comprehension requires `use logos.lang.writ.comp_builder;`");
        return error_expr();
    }

    // The container type is whatever the builder returns (Rc<Writ>).
    TypeRef ctr_t = new_fi->ret_type;

    std::string ctr_var = "__hlc_c_" + std::to_string(tmp_var_count_++);

    push_scope();
    define(ctr_var, ctr_t, true);
    define(std::string(var_name), elem_type, false);
    auto val_expr_body = lower_expr(map_of(node.get(la::VALUE.code)));
    lir::LExprPtr guard_body = nullptr;
    if (node.has_key(la::GUARD))
        guard_body = lower_expr(map_of(node.get(la::GUARD.code)));
    pop_scope();

    // Coerce VALUE to AnyVal (no-op if already AnyVal).
    val_expr_body = coerce_to_writ_anyval(
        std::move(val_expr_body), ctr_var, ctr_t,
        "writ list comprehension element");
    if (!val_expr_body || TypeRef(expr_type(val_expr_body)).kind() == LogosType::Kind::Error)
        return error_expr();

    // Guard must be Bool; any other type (including Error) is rejected here to
    // avoid cascading diagnostics and to prevent an MLIR verification crash
    // from feeding a non-i1 value into cf.cond_br.
    if (guard_body) {
        auto gk = expr_type(guard_body) ? TypeRef(expr_type(guard_body)).kind()
                                   : LogosType::Kind::Error;
        if (gk == LogosType::Kind::Error)
            return error_expr();
        if (gk != LogosType::Kind::Bool) {
            error(std::format(
                "writ list comprehension: guard must be bool (got {})",
                type_str(expr_type(guard_body))));
            return error_expr();
        }
    }

    // SLet: let mut __hlc_c = writ_list_comp_new(128);
    std::string new_sym = new_fi->symbol_name.empty() ? "writ_list_comp_new"
                                                      : new_fi->symbol_name;
    std::vector<lir::LExprPtr> new_args;
    int64_t cap_hint = arr_size > 0 ? (arr_size * 8 + 128) : 128;
    new_args.push_back(builder().lit_int(cap_hint, prim(LogosType::Kind::I64)));
    auto call_new = builder().call(new_sym, {}, std::move(new_args), ctr_t);
    lir::SLet let_c;
    let_c.name   = ctr_var;
    let_c.type   = ctr_t;
    let_c.is_mut = true;
    let_c.value  = std::move(call_new);

    // writ_list_comp_push(&mut __hlc_c, val);
    std::string push_sym = push_fi->symbol_name.empty() ? "writ_list_comp_push"
                                                        : push_fi->symbol_name;
    // push takes `&Rc<Writ>` (shared) — was `&mut Writ`.
    auto recv = builder().addr_of(ctr_var, make_ref(false, ctr_t));
    std::vector<lir::LExprPtr> push_args;
    push_args.push_back(std::move(recv));
    push_args.push_back(std::move(val_expr_body));
    auto push_call = builder().call(push_sym, {}, std::move(push_args), void_t());

    lir::SExprStmt push_stmt;
    push_stmt.expr = std::move(push_call);

    std::vector<lir_view::StmtRef> loop_body;
    if (guard_body) {
        lir::SIf sif;
        sif.cond = std::move(guard_body);
        std::vector<lir_view::StmtRef> then_blk;
        then_blk.push_back(make_stmt_emit(node_line_, std::move(push_stmt)));
        sif.then_ = lir_mirror_block(*cur_prog_, then_blk);
        loop_body.push_back(make_stmt_emit(node_line_, std::move(sif)));
    } else {
        loop_body.push_back(make_stmt_emit(node_line_, std::move(push_stmt)));
    }

    lir::SForEach sfe;
    sfe.var       = std::string(var_name);
    sfe.iter      = std::move(iter);
    sfe.elem_type = elem_type;
    sfe.arr_size  = arr_size;
    sfe.is_slice  = is_slice;
    sfe.body      = lir_mirror_block(*cur_prog_, loop_body);

    std::vector<lir_view::StmtRef> outer;
    outer.push_back(make_stmt_emit(node_line_, std::move(let_c)));
    outer.push_back(make_stmt_emit(node_line_, std::move(sfe)));

    auto result = builder().var_ref(ctr_var, ctr_t);
    return builder().block_expr(lir_mirror_block(*cur_prog_, outer), std::move(result), ctr_t);
}

// Writ map comprehension:  @{kexpr: vexpr for x in iter (if guard)?}
// v1: string keys only (`str`); values must be AnyVal.
// Requires `use logos.mem.writ.ctr;` in scope.
lir::LExprPtr SemaChecker::lower_writ_map_comp(TinyMapView node) {
    auto var_name = str_of(node.get(la::NAME.code));

    lir::LExprPtr iter = node.has_key(la::ITER)
        ? lower_expr(map_of(node.get(la::ITER.code))) : error_expr();
    TypeRef iter_type = expr_type(iter);

    // Short-circuit on upstream error to avoid cascading diagnostics.
    if (TypeRef(iter_type).kind() == LogosType::Kind::Error)
        return error_expr();

    TypeRef elem_type = nullptr;
    int64_t arr_size = 0;
    bool is_slice = false;
    if (TypeRef(iter_type).kind() == LogosType::Kind::Array) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
        arr_size  = (int64_t)TypeRef(iter_type).arr_size();
    } else if (TypeRef(iter_type).kind() == LogosType::Kind::Slice) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
        is_slice  = true;
    } else {
        error(std::format(
            "writ map comprehension: only array/slice iteration supported (got {})",
            type_str(iter_type)));
        return error_expr();
    }

    // writ builder: yields Rc<Writ> (see lang.writ.comp_builder).
    auto new_cands = find_func_candidates("writ_map_comp_new");
    auto put_cands = find_func_candidates("writ_map_comp_put");
    const SemaFuncInfo* new_fi = nullptr;
    const SemaFuncInfo* put_fi = nullptr;
    for (auto* fi : new_cands) if (fi->param_types.size() == 2) { new_fi = fi; break; }
    for (auto* fi : put_cands) if (fi->param_types.size() == 3) { put_fi = fi; break; }
    if (!new_fi || !put_fi) {
        error("writ map comprehension requires `use logos.lang.writ.comp_builder;`");
        return error_expr();
    }

    // The container type is whatever the builder returns (Rc<Writ>).
    TypeRef ctr_t = new_fi->ret_type;

    std::string ctr_var = "__hmc_c_" + std::to_string(tmp_var_count_++);

    push_scope();
    define(ctr_var, ctr_t, true);
    define(std::string(var_name), elem_type, false);
    auto key_expr = lower_expr(map_of(node.get(la::KEY.code)));
    auto val_expr = lower_expr(map_of(node.get(la::VALUE.code)));
    lir::LExprPtr guard_body = nullptr;
    if (node.has_key(la::GUARD))
        guard_body = lower_expr(map_of(node.get(la::GUARD.code)));
    pop_scope();

    // Require KEY to be str (&[u8] slice).  Short-circuit on Error to avoid
    // cascading diagnostics when the key subexpression already failed.
    TypeRef kt = expr_type(key_expr);
    if (kt && TypeRef(kt).kind() == LogosType::Kind::Error)
        return error_expr();
    if (!(kt && TypeRef(kt).kind() == LogosType::Kind::Slice && TypeRef(kt).elem()
              && TypeRef(kt).elem().kind() == LogosType::Kind::U8)) {
        error(std::format(
            "writ map comprehension: key expression must be str (got {})",
            type_str(kt)));
        return error_expr();
    }

    // Coerce VALUE to AnyVal (no-op if already AnyVal).
    val_expr = coerce_to_writ_anyval(
        std::move(val_expr), ctr_var, ctr_t,
        "writ map comprehension value");
    if (!val_expr || TypeRef(expr_type(val_expr)).kind() == LogosType::Kind::Error)
        return error_expr();

    // Guard must be Bool; reject anything else early to avoid MLIR crashes
    // (cf.cond_br requires i1) and to silence cascades when the guard errored.
    if (guard_body) {
        auto gk = expr_type(guard_body) ? TypeRef(expr_type(guard_body)).kind()
                                   : LogosType::Kind::Error;
        if (gk == LogosType::Kind::Error)
            return error_expr();
        if (gk != LogosType::Kind::Bool) {
            error(std::format(
                "writ map comprehension: guard must be bool (got {})",
                type_str(expr_type(guard_body))));
            return error_expr();
        }
    }

    std::string new_sym = new_fi->symbol_name.empty() ? "writ_map_comp_new"
                                                      : new_fi->symbol_name;
    // Byte-cap hint for zone, and slot-count hint for map buckets.
    // For slices (arr_size==0 at compile time) we don't know iter length, so
    // use a generous default to reduce the risk of silent drops.  This is a
    // v1 limitation — objectmap_set has no auto-grow.
    int64_t slot_hint = arr_size > 0 ? arr_size : 64;
    int64_t cap_hint  = arr_size > 0 ? (arr_size * 48 + 256) : 4096;
    std::vector<lir::LExprPtr> new_args;
    new_args.push_back(builder().lit_int(cap_hint, prim(LogosType::Kind::I64)));
    new_args.push_back(builder().lit_int(slot_hint, prim(LogosType::Kind::I64)));
    auto call_new = builder().call(new_sym, {}, std::move(new_args), ctr_t);
    lir::SLet let_c;
    let_c.name   = ctr_var;
    let_c.type   = ctr_t;
    let_c.is_mut = true;
    let_c.value  = std::move(call_new);

    std::string put_sym = put_fi->symbol_name.empty() ? "writ_map_comp_put"
                                                      : put_fi->symbol_name;
    auto recv = builder().addr_of(ctr_var, make_ref(false, ctr_t));  // &Rc<Writ>
    std::vector<lir::LExprPtr> put_args;
    put_args.push_back(std::move(recv));
    put_args.push_back(std::move(key_expr));
    put_args.push_back(std::move(val_expr));
    auto put_call = builder().call(put_sym, {}, std::move(put_args), void_t());

    lir::SExprStmt put_stmt;
    put_stmt.expr = std::move(put_call);

    std::vector<lir_view::StmtRef> loop_body;
    if (guard_body) {
        lir::SIf sif;
        sif.cond = std::move(guard_body);
        std::vector<lir_view::StmtRef> then_blk;
        then_blk.push_back(make_stmt_emit(node_line_, std::move(put_stmt)));
        sif.then_ = lir_mirror_block(*cur_prog_, then_blk);
        loop_body.push_back(make_stmt_emit(node_line_, std::move(sif)));
    } else {
        loop_body.push_back(make_stmt_emit(node_line_, std::move(put_stmt)));
    }

    lir::SForEach sfe;
    sfe.var       = std::string(var_name);
    sfe.iter      = std::move(iter);
    sfe.elem_type = elem_type;
    sfe.arr_size  = arr_size;
    sfe.is_slice  = is_slice;
    sfe.body      = lir_mirror_block(*cur_prog_, loop_body);

    std::vector<lir_view::StmtRef> outer;
    outer.push_back(make_stmt_emit(node_line_, std::move(let_c)));
    outer.push_back(make_stmt_emit(node_line_, std::move(sfe)));

    auto result = builder().var_ref(ctr_var, ctr_t);
    return builder().block_expr(lir_mirror_block(*cur_prog_, outer), std::move(result), ctr_t);
}

// Coerce an arbitrary value to AnyVal for use inside a Writ comprehension.
// Returns the original expr if already AnyVal; otherwise wraps in a call to
// one of the `writ_coerce_*` helpers in writ/ctr.logos. String coercion
// requires `&mut ctr_var` because the string is copied into the zone.
lir::LExprPtr SemaChecker::coerce_to_writ_anyval(
        lir::LExprPtr val,
        const std::string& ctr_var,
        TypeRef ctr_t,
        std::string_view context) {
    if (!val || !expr_type(val)) return val;
    TypeRef t = expr_type(val);

    // Pass Error through unchanged — caller short-circuits on Error without
    // emitting an additional "cannot auto-coerce <error>" diagnostic.
    if (TypeRef(t).kind() == LogosType::Kind::Error) return val;

    // WAny passthrough (writ): an element already produced as an WAny value.
    if (TypeRef(t).kind() == LogosType::Kind::Enum
        && TypeRef(t).enum_name() == "WAny") {
        return val;
    }
    // AnyVal passthrough (legacy datatype/struct form) — legacy.
    if ((TypeRef(t).kind() == LogosType::Kind::Struct
         || TypeRef(t).kind() == LogosType::Kind::ZonedStruct)
        && is_anyval(t)) {
        return val;
    }

    const char* helper = nullptr;
    bool needs_ctr = false;
    using K = LogosType::Kind;
    switch (TypeRef(t).kind()) {
        case K::Bool: helper = "writ_coerce_bool"; break;
        case K::I8:   helper = "writ_coerce_i8";   break;
        case K::I16:  helper = "writ_coerce_i16";  break;
        case K::I32:  case K::IntLit:
                      helper = "writ_coerce_i32"; break;
        case K::U8:   helper = "writ_coerce_u8";   break;
        case K::U16:  helper = "writ_coerce_u16";  break;
        case K::U32:  helper = "writ_coerce_u32"; break;
        // i64/u64/i24/u24/i56/u56/i128/u128 intentionally omitted: embedding
        // them via i32 would silently truncate high bits.  User must cast
        // explicitly (e.g. `x as i32`) or wrap with WAny::from.
        case K::Slice:
            if (TypeRef(t).elem() && TypeRef(t).elem().kind() == K::U8) {
                helper = "writ_coerce_str";
                needs_ctr = true;
            }
            break;
        default: break;
    }

    if (!helper) {
        error(std::format(
            "{}: cannot auto-coerce {} to AnyVal; cast to i32/u32/bool/str "
            "explicitly, or wrap with AnyVal::embed_*",
            context, type_str(t)));
        return error_expr();
    }

    size_t want_arity = needs_ctr ? 2 : 1;
    auto cands = find_func_candidates(helper);
    const SemaFuncInfo* fi = nullptr;
    for (auto* c : cands) {
        if (c->param_types.size() == want_arity) { fi = c; break; }
    }
    if (!fi) {
        error(std::format("{}: {} not found; `use logos.mem.writ.ctr;`",
                          context, helper));
        return error_expr();
    }
    TypeRef ret_t = fi->ret_type;

    std::vector<lir::LExprPtr> args;
    if (needs_ctr) {
        auto recv = builder().addr_of(ctr_var, make_ref(false, ctr_t));  // &Rc<Writ>
        args.push_back(std::move(recv));
    }
    args.push_back(std::move(val));
    std::string sym = fi->symbol_name.empty() ? helper : fi->symbol_name;
    return builder().call(sym, {}, std::move(args), ret_t);
}

lir::LExprPtr SemaChecker::lower_arr_fill_lit(TinyMapView node) {
    auto val_node = map_of(node.get(la::VALUE.code));
    auto fill_val = lower_expr(val_node);
    TypeRef elem_type = expr_type(fill_val);
    // ONE resolver, shared with the type position — which is what makes
    // `[v; K]` with a const-generic K work at last: the expression position
    // used to accept only module consts, so `[u64; K]` compiled as a type and
    // failed as an expression.
    auto len = resolve_array_len(node.has_key(la::SIZE)
                                  ? map_of(node.get(la::SIZE.code))
                                  : writ::TinyMapView{});
    if (!len.ok) return error_expr();
    if (!len.symbolic.empty()) {
        // A length that is not known yet (a symbolic name, or a deferred const
        // EXPRESSION postfix-encoded in `symbolic`): emit ONE element carrying
        // the unresolved size; mono repeats it once the length folds.
        LogosTypeBuilder ab;
        ab.kind = LogosType::Kind::Array;
        ab.elem = elem_type;
        ab.arr_size = 0;
        ab.arr_size_var = len.symbolic;
        TypeRef arr_t = pool_->alloc(std::move(ab));
        std::vector<lir::LExprPtr> one;
        one.push_back(std::move(fill_val));
        return builder().arr_lit(std::move(one), arr_t);
    }
    int64_t n = static_cast<int64_t>(len.value);
    // Keep IntLit unresolved so that struct-literal type inference (hint_struct_type_)
    // can widen the element to the correct concrete type (e.g. i64 for Vec<i64>).
    std::vector<lir::LExprPtr> elems;
    elems.push_back(std::move(fill_val));
    for (int64_t i = 1; i < n; ++i)
        elems.push_back(lower_expr(val_node));  // re-lower for each slot (simple literals)
    return builder().arr_lit(std::move(elems), make_array(elem_type, (size_t)n));
}

// g9/B121: generic associated-const projection `T::CONST`. When T (`cname`) is
// an abstract type-param bounded by a trait that declares `const CONST`
// (`mname`), the concrete value is known only after T is substituted at mono.
// Route the read through a zero-arg accessor call `T__kassoc_CONST()`: mono
// rewrites the `T__` prefix to the concrete type (its existing generic-static
// dispatch ECall rewrite) and codegen calls the per-impl accessor LFunction
// `Concrete__kassoc_CONST` emitted in lower_impl_block. Mirrors the generic-
// static-method dispatch path. Returns nullptr if `cname` is not such a
// projection (caller falls through to its normal "unknown enum" handling).
lir::LExprPtr SemaChecker::try_lower_generic_assoc_const(const std::string& cname,
                                                         const std::string& mname) {
    auto bit = current_type_bounds_.find(cname);
    if (bit == current_type_bounds_.end()) return nullptr;
    // Transitively close the bounds over supertraits (the const may be declared
    // on a supertrait of a stated bound), then find a trait declaring `mname`.
    logos::compiler::StrSet seen;
    std::vector<std::string> search_traits;
    std::function<void(const std::string&)> add_t =
        [&](const std::string& tn) {
            if (!seen.insert(tn).second) return;
            search_traits.push_back(tn);
            auto it = find_trait_iter_scoped(tn);
            if (it != traits_.end())
                for (auto& s : it->second.supertraits) add_t(s.trait_name);
        };
    for (auto& b : bit->second) add_t(b.trait_name);
    for (auto& tn : search_traits) {
        auto tit = find_trait_iter_scoped(tn);
        if (tit == traits_.end()) continue;
        for (auto& ac : tit->second.assoc_consts) {
            if (ac.name != mname) continue;
            TypeRef ret_t = ac.type ? ac.type : prim(LogosType::Kind::I64);
            return builder().call(cname + "__kassoc_" + mname, {}, {}, ret_t);
        }
    }
    return nullptr;
}

lir::LExprPtr SemaChecker::lower_enum_lit(TinyMapView node) {
    std::string ename_buf(str_of(node.get(la::NAME.code)));
    // T2-28 (Increment 2): a fully-qualified no-paren path `pkg.path.Type::member`
    // (unit enum variant or associated const) arrives with NAME = first segment
    // + QUAL_PARTS = the rest; the LAST segment is the type. (FIELD already holds
    // the member.) The package prefix is dropped — lookup is by type name.
    if (node.has_key(la::QUAL_PARTS)) {
        auto parts = arr_of(node.get(la::QUAL_PARTS.code));
        if (parts.size() >= 1)
            ename_buf = std::string(
                str_of(map_of(parts.get(parts.size() - 1)).get(la::NAME.code)));
    }
    // G160-1: `Self::Qux` (unit variant) inside an `impl Enum` body — resolve
    // `Self` to the enclosing enum's name so the variant lookup succeeds (the
    // tuple-variant form `Self::Bar(x)` already resolves via lower_static_call;
    // the struct-shaped form is handled in the struct-lit path).
    if (ename_buf == "Self") {
        auto sit = current_type_params_.find("Self");
        if (sit != current_type_params_.end() && sit->second &&
            TypeRef(sit->second).kind() == LogosType::Kind::Enum)
            ename_buf = std::string(TypeRef(sit->second).enum_name());
    }
    // G160-2: peel a non-generic type-alias to an enum (`type A = Foo; A::Qux`).
    if (!enums_.count(ename_buf) && !find_enum_by_name(ename_buf).second) {
        auto ait = alias_find(ename_buf);
        if (ait != type_aliases_.end() && ait->second.type_params.empty() &&
            ait->second.type &&
            TypeRef(ait->second.type).kind() == LogosType::Kind::Enum)
            ename_buf = std::string(TypeRef(ait->second.type).enum_name());
    }
    std::string_view ename = ename_buf;
    auto vname = str_of(node.get(la::FIELD.code));
    auto [epkg_el, esi_el] = find_enum_by_name(ename);
    auto eit = esi_el ? enums_.find(sema_key(epkg_el, std::string(ename))) : enums_.end();
    if (eit == enums_.end()) eit = enums_.find(std::string(ename));
    if (eit == enums_.end()) {
        // Before reporting "unknown enum", check if this is an associated constant
        // access (e.g. Buffer::MAX) parsed as ENUM_LIT due to grammar ambiguity.
        std::string cname_str = std::string(ename);
        std::string mname_str = std::string(vname);
        // B97: try inherent assoc-const first (`impl S { const C: T = ... }`).
        {
            std::string key = "inherent::" + cname_str + "::" + mname_str;
            auto cit = assoc_const_impls_.find(key);
            if (cit != assoc_const_impls_.end()) {
                if (!cit->second.cached_value) {
                    auto val = lower_expr(map_of(cit->second.value_ast));
                    if (cit->second.type) builder().retype_expr(val, cit->second.type);
                    cit->second.cached_value = val;
                }
                return cit->second.cached_value;
            }
        }
        for (auto& [tname, tinfo] : traits_) {
            if (!impls_.count(tname + "::" + cname_str)) continue;
            std::string key = tname + "::" + cname_str + "::" + mname_str;
            auto cit = assoc_const_impls_.find(key);
            if (cit != assoc_const_impls_.end()) {
                if (!cit->second.cached_value) {
                    auto val = lower_expr(map_of(cit->second.value_ast));
                    if (cit->second.type) builder().retype_expr(val, cit->second.type);
                    cit->second.cached_value = val;
                }
                return cit->second.cached_value;
            }
        }
        // g9/B121: generic assoc-const projection `T::CONST` (T a bound
        // type-param) — route through a per-impl accessor call.
        if (auto acc = try_lower_generic_assoc_const(cname_str, mname_str))
            return acc;
        // Method as a fn-pointer value: `let f = A::bar;` — an INHERENT method
        // path in value position (not a call). Resolve `A__bar` and emit a FnPtr
        // var-ref over its signature, mirroring the bare-fn-name path in
        // lower_var_ref. The call site `f(&a)` then dispatches as a fn-ptr call.
        {
            std::string msym = cname_str + "__" + mname_str;
            const SemaFuncInfo* mfi = nullptr;
            auto mcands = find_func_candidates(msym);
            if (mcands.size() == 1) mfi = mcands[0];
            // G172-13: trait-qualified form `Foo::foo` (Foo a trait, not a
            // type). Rust infers Self from the call site; we resolve it only when
            // there's exactly ONE impl of the trait in scope (unambiguous — Rust
            // also errors on a trait method fn-ptr with multiple candidate Selfs
            // and no annotation). The unique impl's `<Concrete>__foo` supplies
            // the signature.
            if (!mfi && find_trait_by_name(cname_str).second) {
                std::string prefix = cname_str + "::";
                std::string sole_concrete;
                int n_impls = 0;
                for (auto& [k, _v] : impls_) {
                    if (k.size() <= prefix.size() ||
                        k.compare(0, prefix.size(), prefix) != 0) continue;
                    std::string concrete = k.substr(prefix.size());
                    // Skip trait-arg-keyed coherence entries (contain '[').
                    if (concrete.find('[') != std::string::npos) continue;
                    if (concrete != sole_concrete) { ++n_impls; sole_concrete = concrete; }
                }
                if (n_impls == 1) {
                    auto cc = find_func_candidates(sole_concrete + "__" + mname_str);
                    if (cc.size() == 1) { mfi = cc[0]; msym = sole_concrete + "__" + mname_str; }
                }
            }
            if (mfi && mfi->type_params.empty()) {
                LogosTypeBuilder ft;
                ft.kind = LogosType::Kind::FnPtr;
                for (auto pt : mfi->param_types) ft.closure_params.push_back(pt);
                ft.closure_ret = mfi->ret_type ? mfi->ret_type : void_t();
                auto fn_type = pool_->alloc(std::move(ft));
                return builder().var_ref(
                    mfi->symbol_name.empty() ? msym : mfi->symbol_name, fn_type);
            }
        }
        error(std::format("unknown enum '{}'", ename));
        return error_expr();
    }
    int64_t disc = 0;
    bool found = false;
    for (auto& v : eit->second.variants)
        if (v.name == vname) { disc = v.value; found = true; break; }
    if (!found) {
        // B97: enum exists but variant not found — could be assoc const
        // on an enum (rare). Try inherent lookup as last resort.
        std::string key = "inherent::" + std::string(ename) + "::" + std::string(vname);
        auto cit = assoc_const_impls_.find(key);
        if (cit != assoc_const_impls_.end()) {
            if (!cit->second.cached_value) {
                auto val = lower_expr(map_of(cit->second.value_ast));
                if (cit->second.type) builder().retype_expr(val, cit->second.type);
                cit->second.cached_value = val;
            }
            return cit->second.cached_value;
        }
        error(std::format("enum '{}' has no variant '{}'", ename, vname));
        return error_expr();
    }
    // SL-sl-03: a payload-less variant (`Option::None`) on a generic enum
    // has no inference source for its type-args. Consult `hint_enum_type_`
    // (set by the call-site / let / deref-write context) so the resulting
    // type is `Option<i32>` instead of bare `Option` — mlir-gen needs the
    // concrete name to find the tagged-enum layout.
    TypeRef result_t = make_enum_type(ename, epkg_el);
    auto& einfo_for_hint = eit->second;
    if (!einfo_for_hint.type_params.empty() &&
        hint_enum_type_ &&
        TypeRef(hint_enum_type_).kind() == LogosType::Kind::Enum &&
        TypeRef(hint_enum_type_).enum_name() == std::string(ename) &&
        TypeRef(hint_enum_type_).type_args().size() == einfo_for_hint.type_params.size()) {
        std::vector<TypeRef> targs;
        for (auto a : TypeRef(hint_enum_type_).type_args()) targs.push_back(a);
        std::vector<std::string> lt_args;
        for (auto& lp : einfo_for_hint.lifetime_params) {
            (void)lp;
            lt_args.push_back(std::string{});
        }
        result_t = make_generic_enum(std::string(ename), std::move(targs), std::move(lt_args));
    }
    return builder().enum_lit(std::string(ename), std::string(vname), disc, result_t);
}

lir::LExprPtr SemaChecker::lower_enum_lit_data(TinyMapView node) {
    std::string ename_buf(str_of(node.get(la::NAME.code)));
    // G160-1: `Self::Baz { .. }` / `Self::Bar(x)` inside an `impl Enum` body —
    // resolve `Self` to the enclosing enum name (mirrors lower_enum_lit).
    if (ename_buf == "Self") {
        auto sit = current_type_params_.find("Self");
        if (sit != current_type_params_.end() && sit->second &&
            TypeRef(sit->second).kind() == LogosType::Kind::Enum)
            ename_buf = std::string(TypeRef(sit->second).enum_name());
    }
    // G160-2: peel a non-generic type-alias to an enum.
    if (!enums_.count(ename_buf) && !find_enum_by_name(ename_buf).second) {
        auto ait = alias_find(ename_buf);
        if (ait != type_aliases_.end() && ait->second.type_params.empty() &&
            ait->second.type &&
            TypeRef(ait->second.type).kind() == LogosType::Kind::Enum)
            ename_buf = std::string(TypeRef(ait->second.type).enum_name());
    }
    std::string_view ename = ename_buf;
    auto vname = str_of(node.get(la::FIELD.code));
    auto [epkg_eld, esi_eld] = find_enum_by_name(ename);
    auto eit = esi_eld ? enums_.find(sema_key(epkg_eld, std::string(ename))) : enums_.end();
    if (eit == enums_.end()) eit = enums_.find(std::string(ename));
    if (eit == enums_.end()) {
        // Bug 5 fix: ENUM_LIT_DATA shares the same grammar path as ENUM_LIT.
        // Check for associated constant access before reporting "unknown enum".
        std::string cname_str = std::string(ename);
        std::string mname_str = std::string(vname);
        for (auto& [tname, tinfo] : traits_) {
            if (!impls_.count(tname + "::" + cname_str)) continue;
            std::string key = tname + "::" + cname_str + "::" + mname_str;
            auto cit = assoc_const_impls_.find(key);
            if (cit != assoc_const_impls_.end()) {
                if (!cit->second.cached_value) {
                    auto val = lower_expr(map_of(cit->second.value_ast));
                    if (cit->second.type) builder().retype_expr(val, cit->second.type);
                    cit->second.cached_value = val;
                }
                return cit->second.cached_value;
            }
        }
        // g9/B121: generic assoc-const projection `T::CONST` (T a bound
        // type-param) — route through a per-impl accessor call.
        if (auto acc = try_lower_generic_assoc_const(cname_str, mname_str))
            return acc;
        // Method as a fn-pointer value: `let f = A::bar;` — an INHERENT method
        // path in value position (not a call). Resolve `A__bar` and emit a FnPtr
        // var-ref over its signature, mirroring the bare-fn-name path in
        // lower_var_ref. The call site `f(&a)` then dispatches as a fn-ptr call.
        {
            std::string msym = cname_str + "__" + mname_str;
            const SemaFuncInfo* mfi = nullptr;
            auto mcands = find_func_candidates(msym);
            if (mcands.size() == 1) mfi = mcands[0];
            // G172-13: trait-qualified form `Foo::foo` (Foo a trait, not a
            // type). Rust infers Self from the call site; we resolve it only when
            // there's exactly ONE impl of the trait in scope (unambiguous — Rust
            // also errors on a trait method fn-ptr with multiple candidate Selfs
            // and no annotation). The unique impl's `<Concrete>__foo` supplies
            // the signature.
            if (!mfi && find_trait_by_name(cname_str).second) {
                std::string prefix = cname_str + "::";
                std::string sole_concrete;
                int n_impls = 0;
                for (auto& [k, _v] : impls_) {
                    if (k.size() <= prefix.size() ||
                        k.compare(0, prefix.size(), prefix) != 0) continue;
                    std::string concrete = k.substr(prefix.size());
                    // Skip trait-arg-keyed coherence entries (contain '[').
                    if (concrete.find('[') != std::string::npos) continue;
                    if (concrete != sole_concrete) { ++n_impls; sole_concrete = concrete; }
                }
                if (n_impls == 1) {
                    auto cc = find_func_candidates(sole_concrete + "__" + mname_str);
                    if (cc.size() == 1) { mfi = cc[0]; msym = sole_concrete + "__" + mname_str; }
                }
            }
            if (mfi && mfi->type_params.empty()) {
                LogosTypeBuilder ft;
                ft.kind = LogosType::Kind::FnPtr;
                for (auto pt : mfi->param_types) ft.closure_params.push_back(pt);
                ft.closure_ret = mfi->ret_type ? mfi->ret_type : void_t();
                auto fn_type = pool_->alloc(std::move(ft));
                return builder().var_ref(
                    mfi->symbol_name.empty() ? msym : mfi->symbol_name, fn_type);
            }
        }
        // `Z::method::<T..>(args)` — Z a type-param bound by a trait declaring a
        // static `method`. The grammar parses this identically to a generic
        // enum-variant construction; disambiguate when NAME is a bound type-param.
        if (current_type_bounds_.count(cname_str)) {
            std::vector<lir::LExprPtr> sargs;
            if (node.has_key(la::ARGS)) {
                AnyVal aav = node.get(la::ARGS.code);
                auto run = [&](auto items) {
                    for (uint64_t i = 0; i < items.size(); ++i)
                        sargs.push_back(lower_expr(map_of(items.get(i))));
                };
                if (!aav.is_null()) {
                    if (aav.is_pointer()) {
                        auto mm = map_of(aav);
                        if (mm.has_key(la::ITEMS)) run(arr_of(mm.get(la::ITEMS.code)));
                        else                       run(arr_of(aav));
                    } else run(arr_of(aav));
                }
            }
            if (auto c = lower_typaram_static_method(cname_str, mname_str,
                            collect_type_args(node), std::move(sargs)))
                return c;
        }
        error(std::format("unknown enum '{}'", ename));
        return error_expr();
    }
    const SemaVariantInfo* vinfo = nullptr;
    for (auto& v : eit->second.variants)
        if (v.name == vname) { vinfo = &v; break; }
    if (!vinfo) {
        error(std::format("enum '{}' has no variant '{}'", ename, vname));
        return error_expr();
    }

    // Lower payload arguments. ARGS is either a direct array (legacy
    // `enum_lit` alt with `$...`) or a { ITEMS: [...] } map (turbofish
    // alt routes through enum_lit_args sub-production). Accept both.
    std::vector<lir::LExprPtr> payload;
    bool is_struct_shape_lit = node.has_key(la::variant::IS_STRUCT_SHAPE) &&
        node.get(la::variant::IS_STRUCT_SHAPE.code).as_value<int32_t>() != 0;
    if (is_struct_shape_lit) {
        // P4-pm-01: `E::V { name: expr, ... }` — items list of
        // FIELD_INIT / FIELD_SHORTHAND. Resolve names → variant
        // payload positions via the variant's payload_field_names,
        // produce positional `payload` in declaration order.
        // An empty struct-shape variant (`E::Empty {}`) legitimately has no
        // fields — gate on the declared shape, not on field-count, so the
        // empty case is accepted (produces an empty payload).
        if (!vinfo->is_struct_shape && vinfo->payload_field_names.empty()) {
            error(std::format(
                "{}::{} is not a struct-shape variant — use `{}::{}({{args...}})` or no payload",
                ename, vname, ename, vname));
        }
        size_t arity = vinfo->payload_field_names.size();
        std::vector<lir::LExprPtr> by_pos(arity);
        std::vector<bool> seen(arity, false);
        std::vector<std::string> provided;
        if (node.has_key(la::ITEMS)) {
            auto items_av = node.get(la::ITEMS.code);
            ArrayView fitems;
            if (!items_av.is_null()) {
                if (items_av.is_pointer()) {
                    auto m = map_of(items_av);
                    if (m.has_key(la::ITEMS)) fitems = arr_of(m.get(la::ITEMS.code));
                    else                       fitems = arr_of(items_av);
                } else {
                    fitems = arr_of(items_av);
                }
            }
            for (uint64_t i = 0; i < fitems.size(); ++i) {
                auto fnode = map_of(fitems.get(i));
                std::string fname;
                if (fnode.has_key(la::NAME))
                    fname = std::string(str_of(fnode.get(la::NAME.code)));
                // Locate field index in variant declaration order.
                size_t idx = arity;
                for (size_t k = 0; k < arity; ++k)
                    if (vinfo->payload_field_names[k] == fname) { idx = k; break; }
                if (idx == arity) {
                    error(std::format("{}::{}: no field named '{}'",
                          ename, vname, fname));
                    continue;
                }
                if (seen[idx]) {
                    error(std::format("{}::{}: field '{}' specified more than once",
                          ename, vname, fname));
                    continue;
                }
                // FIELD_INIT carries VALUE; FIELD_SHORTHAND is `name` only —
                // synthesise an EVarRef of the same name.
                // The variant field's declared payload type — a CONCRETE enum
                // hints a payload-less enum-literal value (`E::V { f: None }`)
                // so it heap-allocates with the right monomorphisation instead
                // of lowering as a bare C-style enum the heap-ptr slot mis-reads.
                TypeRef fld_decl_ty = (idx < vinfo->payload_types.size())
                    ? vinfo->payload_types[idx] : TypeRef(nullptr);
                if (fld_decl_ty && !eit->second.type_params.empty() && hint_enum_type_ &&
                    TypeRef(hint_enum_type_).enum_name() == ename) {
                    SemaSubst fs;
                    auto hta = TypeRef(hint_enum_type_).type_args();
                    for (size_t k = 0; k < eit->second.type_params.size() && k < hta.size(); ++k)
                        if (hta[k]) fs[eit->second.type_params[k].name] = hta[k];
                    if (!fs.empty()) fld_decl_ty = subst_type_sema(fld_decl_ty, fs);
                }
                bool fld_concrete_enum = fld_decl_ty &&
                    TypeRef(fld_decl_ty).kind() == LogosType::Kind::Enum &&
                    !TypeRef(fld_decl_ty).type_args().empty() &&
                    !enum_arg_unresolved(fld_decl_ty);
                lir::LExprPtr val = nullptr;
                int32_t fcode = code_of(fnode);
                if (fnode.has_key(la::VALUE)) {
                    TypeRef saved_eh = hint_enum_type_;
                    if (fld_concrete_enum) hint_enum_type_ = fld_decl_ty;
                    val = lower_expr(map_of(fnode.get(la::VALUE.code)));
                    hint_enum_type_ = saved_eh;
                    if (fld_concrete_enum) try_retype_bare_enum_arg(val, fld_decl_ty);
                } else if (fcode == la::FIELD_SHORTHAND) {
                    TypeRef rt = lookup(fname);
                    if (!rt) {
                        error(std::format("{}::{}: shorthand '{}' — name not in scope",
                              ename, vname, fname));
                        val = error_expr();
                    } else {
                        val = builder().var_ref(fname, rt);
                    }
                } else {
                    error(std::format("{}::{}: internal — field-init missing VALUE", ename, vname));
                    val = error_expr();
                }
                by_pos[idx] = std::move(val);
                seen[idx] = true;
                provided.push_back(fname);
            }
        }
        (void)provided;
        // Missing-field diagnostics — report all in one shot.
        std::vector<std::string> missing;
        for (size_t k = 0; k < arity; ++k)
            if (!seen[k])
                missing.push_back(vinfo->payload_field_names[k]);
        if (!missing.empty()) {
            std::string list;
            for (size_t k = 0; k < missing.size(); ++k) {
                if (k) list += ", ";
                list += "'" + missing[k] + "'";
            }
            error(std::format("{}::{}: missing field(s): {}", ename, vname, list));
        }
        for (auto& p : by_pos) {
            if (!p) p = error_expr();
            payload.push_back(std::move(p));
        }
    } else if (node.has_key(la::ARGS)) {
        AnyVal args_av = node.get(la::ARGS.code);
        // K4-root: project the outer hint's type-args through this variant's
        // payload TypeVars so a nested payload-less enum literal
        // (`Option::Some(Option::None)` with hint `Option<Option<i64>>`) gets
        // its OWN concrete hint (`Option<i64>`) and heap-allocates with the
        // right monomorphisation — otherwise the inner `None` stays a bare
        // C-style enum and the outer slot (a heap-ptr) reads garbage at deref.
        // Mirrors lower_enum_lit_data_from_static's pre_subst narrowing.
        SemaSubst pre_subst;
        if (hint_enum_type_ &&
            TypeRef(hint_enum_type_).kind() == LogosType::Kind::Enum &&
            TypeRef(hint_enum_type_).enum_name() == std::string(ename) &&
            !eit->second.type_params.empty()) {
            auto hta = TypeRef(hint_enum_type_).type_args();
            for (size_t k = 0; k < eit->second.type_params.size() && k < hta.size(); ++k) {
                if (!hta[k] || TypeRef(hta[k]).kind() == LogosType::Kind::Error) continue;
                pre_subst[eit->second.type_params[k].name] = hta[k];
            }
        }
        auto run = [&](auto items) {
            for (uint64_t i = 0; i < items.size(); ++i) {
                TypeRef saved_hint = hint_enum_type_;
                if (i < vinfo->payload_types.size()) {
                    TypeRef pt_i = vinfo->payload_types[i];
                    if (pt_i && !pre_subst.empty())
                        pt_i = subst_type_sema(pt_i, pre_subst);
                    if (pt_i && TypeRef(pt_i).kind() == LogosType::Kind::Enum)
                        hint_enum_type_ = pt_i;
                }
                auto e = lower_expr(map_of(items.get(i)));
                hint_enum_type_ = saved_hint;
                if (TypeRef(expr_type(e)).kind() == LogosType::Kind::Void) continue;
                payload.push_back(std::move(e));
            }
        };
        if (!args_av.is_null()) {
            if (args_av.is_pointer()) {
                auto m = map_of(args_av);
                if (m.has_key(la::ITEMS)) run(arr_of(m.get(la::ITEMS.code)));
                else                       run(arr_of(args_av));
            } else {
                run(arr_of(args_av));
            }
        }
    }

    // Resolve payload types — substitute TypeVars if generic enum
    auto& einfo = eit->second;
    std::vector<TypeRef> resolved_payload_types = vinfo->payload_types;

    // B81: infer enum's lifetime args from paired payload types. Walk each
    // (declared payload type, actual value type) pair extracting lifetimes
    // from Refs and mapping back to einfo.lifetime_params. Used to populate
    // lifetime_args on the constructed enum type so the variance check at
    // return-site / let-init can compare lifetimes.
    std::unordered_map<std::string, std::string> lt_subst;
    LtCands lt_cands;
    {
        std::function<void(TypeRef, TypeRef)> walk = [&](TypeRef pt, TypeRef at) {
            if (!pt || !at) return;
            using K = LogosType::Kind;
            auto pk = pt.kind();
            if ((pk == K::Ref || pk == K::MutRef) && (at.kind() == K::Ref || at.kind() == K::MutRef)) {
                std::string pl(pt.lifetime()), al(at.lifetime());
                if (!pl.empty() && !al.empty()) {
                    lt_cands[pl].push_back(al);
                    if (!lt_subst.count(pl)) lt_subst[pl] = al;
                }
                walk(pt.pointee(), at.pointee());
                return;
            }
            if (pk == K::Tuple && at.kind() == K::Tuple) {
                auto pe = pt.tuple_elems(); auto ae = at.tuple_elems();
                for (size_t i = 0; i < pe.size() && i < ae.size(); ++i) walk(pe[i], ae[i]);
                return;
            }
            if ((pk == K::Struct || pk == K::ZonedStruct || pk == K::Enum) && at.kind() == pk) {
                auto pl = pt.lifetime_args(); auto al = at.lifetime_args();
                for (size_t i = 0; i < pl.size() && i < al.size(); ++i) {
                    if (!pl[i].empty() && !al[i].empty() && !lt_subst.count(pl[i]))
                        lt_subst[pl[i]] = al[i];
                }
                auto pa = pt.type_args(); auto aa = at.type_args();
                for (size_t i = 0; i < pa.size() && i < aa.size(); ++i) walk(pa[i], aa[i]);
                return;
            }
        };
        for (size_t i = 0; i < vinfo->payload_types.size() && i < payload.size(); ++i)
            if (payload[i]) walk(vinfo->payload_types[i], expr_type(payload[i]));
        census_meet_("enumlit", einfo.lifetime_params, lt_cands, eit->first);
    }

    // Build the enum type (may be generic, e.g. Option<i32>)
    // For now, if the enum has type params, we need to infer them from payload types.
    // Simple inference: match payload args to payload type params.
    TypeRef result_type = make_enum_type(ename);
    if (!einfo.type_params.empty()) {
        // Build substitution from payload args. When a payload is an unresolved
        // integer/float literal AND the let/return hint pins that type-param to
        // a concrete type (`let a: MyOpt<i64> = MyOpt::MSome(3)`), prefer the
        // hint over the i32/f64 literal default — otherwise `3` would default
        // the enum's T to i32 and the annotation's i64 would be ignored, so
        // a downstream `impl<T> ... for MyOpt<T>` method instantiates at the
        // wrong T (root cause of G150-2's generic-enum-`==` blocker).
        bool hint_matches = hint_enum_type_ &&
            TypeRef(hint_enum_type_).enum_name() == std::string(ename) &&
            !TypeRef(hint_enum_type_).type_args().empty();
        auto hint_for_param = [&](std::string_view tvname) -> TypeRef {
            if (!hint_matches) return nullptr;
            for (size_t k = 0; k < einfo.type_params.size() &&
                               k < TypeRef(hint_enum_type_).type_args().size(); ++k)
                if (einfo.type_params[k].name == tvname) {
                    auto h = TypeRef(hint_enum_type_).type_args()[k];
                    if (h && TypeRef(h).kind() != LogosType::Kind::Error) return h;
                    break;
                }
            return nullptr;
        };
        SemaSubst subst;
        for (size_t i = 0; i < vinfo->payload_types.size() && i < payload.size(); ++i) {
            auto pt = vinfo->payload_types[i];
            if (pt && TypeRef(pt).kind() == LogosType::Kind::TypeVar) {
                auto inferred = expr_type(payload[i]);
                std::string tvn(TypeRef(pt).type_var_name());
                if (TypeRef(inferred).kind() == LogosType::Kind::IntLit ||
                    TypeRef(inferred).kind() == LogosType::Kind::FloatLit) {
                    if (auto h = hint_for_param(tvn)) {
                        inferred = h;                  // annotation wins
                        widen_int_expr(payload[i], h, builder());  // pin the literal too
                    } else {
                        inferred = TypeRef(inferred).kind() == LogosType::Kind::FloatLit
                                   ? prim(LogosType::Kind::F64) : i32_t();
                    }
                }
                // G168-A: when the hint pins this type-param to a trait object
                // (`Option<Box<dyn Sh>>`) but the arg is a CONCRETE coercible
                // value (`Box<Sq>`), record the enum's type-arg as the DYN type
                // — so the constructed enum is genuinely `Option<Box<dyn Sh>>` —
                // while leaving the payload expr concrete. mlir-gen's
                // enum-payload store then unsize-fattens the concrete payload
                // into the dyn slot; otherwise a thin `Box<Sq>` is stored and
                // dispatch reads a garbage vtable (SIGSEGV).
                auto wraps_dyn = [&](TypeRef t) -> bool {
                    if (!t) return false;
                    TypeRef u(t);
                    if ((u.kind() == LogosType::Kind::Ref ||
                         u.kind() == LogosType::Kind::MutRef) && u.pointee())
                        u = u.pointee();
                    if (is_stdlib_box(u) && u.type_args().size() == 1)
                        u = u.type_args()[0];
                    return u.kind() == LogosType::Kind::TraitObject;
                };
                if (auto h = hint_for_param(tvn))
                    if (wraps_dyn(h) && !wraps_dyn(inferred) &&
                        types_compatible(inferred, h))
                        inferred = h;
                subst[tvn] = inferred;
            } else if (pt) {
                // N8: the declared payload type is a STRUCTURAL type that
                // mentions the enum's type params (e.g. `Full(Pair<T>)`), not a
                // bare TypeVar. Unify it against the actual arg type to extract
                // the nested bindings (`Pair<T>` vs `Pair<i64>` → T=i64) so a
                // turbofish/annotation isn't required for inference.
                unify_types(pt, expr_type(payload[i]), subst);
            }
        }
        // Fill any still-unresolved type params from hint (e.g. let e: Result<i32,i32> = Result::Err(-1))
        if (hint_enum_type_ && TypeRef(hint_enum_type_).enum_name() == std::string(ename)) {
            for (size_t i = 0; i < einfo.type_params.size() && i < TypeRef(hint_enum_type_).type_args().size(); ++i) {
                if (subst.find(einfo.type_params[i].name) == subst.end()) {
                    auto hta = TypeRef(hint_enum_type_).type_args()[i];
                    if (hta && TypeRef(hta).kind() != LogosType::Kind::Error)
                        subst[einfo.type_params[i].name] = hta;
                }
            }
        }
        // Build concrete type args
        std::vector<TypeRef> type_args;
        for (auto& tp : einfo.type_params) {
            auto sit = subst.find(tp.name);
            type_args.push_back(sit != subst.end() ? sit->second : error_t());
        }
        check_type_bounds(std::string(ename), einfo.type_params, type_args);
        // B81: emit lifetime_args from lt_subst (enum's lifetime_params
        // → inferred caller lifetimes). Use empty string for unresolved.
        std::vector<std::string> lt_args;
        for (auto& lp : einfo.lifetime_params) {
            auto it = lt_subst.find(lp);
            lt_args.push_back(it != lt_subst.end() ? it->second : std::string{});
        }
        result_type = make_generic_enum(ename, std::move(type_args), std::move(lt_args));
        // Resolve payload types with substitution
        for (size_t i = 0; i < resolved_payload_types.size(); ++i)
            resolved_payload_types[i] = subst_type_sema(resolved_payload_types[i], subst);
    } else if (!einfo.lifetime_params.empty()) {
        // Non-generic enum but has lifetime params — emit lt_args still.
        std::vector<std::string> lt_args;
        for (auto& lp : einfo.lifetime_params) {
            auto it = lt_subst.find(lp);
            lt_args.push_back(it != lt_subst.end() ? it->second : std::string{});
        }
        result_type = make_generic_enum(ename, {}, std::move(lt_args));
    }

    // Type-check payload args against expected types
    if (!vinfo->is_variadic && payload.size() != vinfo->payload_types.size()) {
        error(std::format("{}::{} expects {} args, got {}",
              ename, vname, vinfo->payload_types.size(), payload.size()));
    } else if (!vinfo->is_variadic) {
        for (size_t i = 0; i < payload.size(); ++i) {
            if (TypeRef(expr_type(payload[i])).kind() != LogosType::Kind::Error &&
                resolved_payload_types[i] &&
                TypeRef(resolved_payload_types[i]).kind() != LogosType::Kind::Error &&
                // #95: `|| aggregate_unsize_pending(...)`. This guard asks
                // types_compatible, which BLANKET-ACCEPTS a thin aggregate
                // against a fat-`&dyn` one — so for exactly the #68/#95 shape
                // `expect_type` was never entered, and with it neither the
                // literal stamp (retype_aggregate_lit_to) nor the refusal.
                // MEASURED: `E::Some((&a,7i64))` at `(&dyn Shape,i64)` wrote an
                // object file and ran rc=139; the hoisted `E::Some(t)` twin the
                // same. With the disjunct the literal COERCES (42) and the
                // hoisted value is REFUSED.
                (!types_compatible(expr_type(payload[i]), resolved_payload_types[i]) ||
                 aggregate_unsize_pending(resolved_payload_types[i], expr_type(payload[i]))))
                // An enum payload is a constructed aggregate's FIELD, like the
                // tuple-struct ctor arm above — not a CoercePos::Operand.
                expect_type(payload[i], resolved_payload_types[i], CoercePos::StructLitField,
                            std::format("{}::{} arg {}:", ename, vname, i));
            // Check IntLit payload value fits in the declared payload type.
            if (resolved_payload_types[i] && TypeRef(expr_type(payload[i])).kind() == LogosType::Kind::IntLit)
                if (auto v = get_intlit_value(payload[i]))
                    if (!intlit_fits(*v, TypeRef(resolved_payload_types[i]).kind()))
                        error(std::format("{}::{} arg {}: value {} does not fit in {}",
                              ename, vname, i, *v, type_str(resolved_payload_types[i])));
            // Check array literal elements against narrow array payload type.
            if (resolved_payload_types[i] &&
                TypeRef(resolved_payload_types[i]).kind() == LogosType::Kind::Array &&
                TypeRef(resolved_payload_types[i]).elem() &&
                TypeRef(expr_type(payload[i])).kind() == LogosType::Kind::Array) {
                auto vr = expr_ref_of(payload[i]);
                if (vr.kind() == lir_schema::expr::Code::ArrLit) {
                    lir_view::EArrLitView al{vr};
                    for (uint64_t ei = 0; ei < al.count(); ++ei) {
                        auto el = al.elem(ei);
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (!intlit_fits(*v, TypeRef(resolved_payload_types[i]).elem().kind()))
                                    error(std::format("{}::{} arg {}: array element {}: value {} does not fit in {}",
                                          ename, vname, i, ei, *v, type_str(TypeRef(resolved_payload_types[i]).elem())));
                    }
                }
            }
            // Check tuple literal elements against narrow tuple payload type.
            if (resolved_payload_types[i] &&
                TypeRef(resolved_payload_types[i]).kind() == LogosType::Kind::Tuple &&
                TypeRef(expr_type(payload[i])).kind() == LogosType::Kind::Tuple) {
                auto vr = expr_ref_of(payload[i]);
                if (vr.kind() == lir_schema::expr::Code::TupleLit) {
                    lir_view::ETupleLitView tl{vr};
                    uint64_t ei = 0;
                    tl.each_elem([&](lir_view::ExprRef el) {
                        if (ei >= TypeRef(resolved_payload_types[i]).tuple_elems().size()) { ++ei; return; }
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (TypeRef(resolved_payload_types[i]).tuple_elems()[ei] &&
                                    !intlit_fits(*v, TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).kind()))
                                    error(std::format("{}::{} arg {}: tuple element {}: value {} does not fit in {}",
                                          ename, vname, i, ei, *v, type_str(TypeRef(resolved_payload_types[i]).tuple_elems()[ei])));
                        if (TypeRef(resolved_payload_types[i]).tuple_elems()[ei] && TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).kind() == LogosType::Kind::Array &&
                            TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                            el.kind() == lir_schema::expr::Code::ArrLit) {
                            lir_view::EArrLitView ial{el};
                            for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                                auto iel = ial.elem(ii);
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (!intlit_fits(*v, TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).elem().kind()))
                                            error(std::format("{}::{} arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                  ename, vname, i, ei, ii, *v, type_str(TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).elem())));
                            }
                        }
                        if (TypeRef(resolved_payload_types[i]).tuple_elems()[ei] && TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).kind() == LogosType::Kind::Tuple &&
                            el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                            el.kind() == lir_schema::expr::Code::TupleLit) {
                            lir_view::ETupleLitView itl{el};
                            uint64_t ii = 0;
                            itl.each_elem([&](lir_view::ExprRef iel) {
                                if (ii >= TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems().size()) { ++ii; return; }
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems()[ii]).kind()))
                                            error(std::format("{}::{} arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                  ename, vname, i, ei, ii, *v, type_str(TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems()[ii])));
                                ++ii;
                            });
                        }
                        ++ei;
                    });
                }
            }
        }
    } else {
        // Variadic variant: match each arg against the pack's type (if it's not a generic expansion itself).
        if (!resolved_payload_types.empty()) {
            auto pack_t = resolved_payload_types[0];
            for (size_t i = 0; i < payload.size(); ++i) {
                if (TypeRef(expr_type(payload[i])).kind() != LogosType::Kind::Error &&
                    TypeRef(pack_t).kind() != LogosType::Kind::Error &&
                    !types_compatible(expr_type(payload[i]), pack_t))
                    expect_type(payload[i], pack_t, CoercePos::StructLitField,
                                std::format("{}::{} variadic arg {}:", ename, vname, i));
            }
        }
    }

    // Move semantics: enum payload elements consume their source — same
    // pattern as struct lit field values. Without this, `Option::Some(v)`
    // for move-type v leaves v live in surrounding scope → double-drop
    // when both v's auto-Drop and the Option's payload-walk drop fire on
    // the same backing.
    for (auto& p : payload) {
        if (p && is_move_type(expr_type(p)))
            mark_moved_expr(expr_ref_of(p));
    }

    return builder().enum_lit_data(std::string(ename), std::string(vname), vinfo->value, std::move(payload), result_type);
}

lir::LExprPtr SemaChecker::lower_enum_lit_data_from_static(
        TinyMapView node, std::string_view ename, std::string_view vname) {
    auto [epkg_els, esi_els] = find_enum_by_name(ename);
    auto eit = esi_els ? enums_.find(sema_key(epkg_els, std::string(ename))) : enums_.end();
    if (eit == enums_.end()) eit = enums_.find(std::string(ename));
    if (eit == enums_.end()) return error_expr();
    const SemaVariantInfo* vinfo = nullptr;
    for (auto& v : eit->second.variants)
        if (v.name == vname) { vinfo = &v; break; }
    if (!vinfo) {
        error(std::format("enum '{}' has no variant '{}'", ename, vname));
        return error_expr();
    }
    // Lower args. ARGS is either:
    //  - a direct array (CALL / ENUM_LIT_DATA bare alt via `$...`)
    //  - a { ITEMS: [...] } map (turbofish / enum_lit_args sub-production)
    // Accept both.
    //
    // Note: do NOT filter void-typed payload here. `Result::Ok(())` etc.
    // wants the unit literal as a real payload entry.
    // CP-cm-19 follow-up: derive a per-payload hint by projecting the outer
    // hint's type-args through the variant's payload TypeVars. Without this,
    // `Option::Some(Result::Ok(42))` lowers the inner `Result::Ok` with the
    // outer's `Option<Result<i32,i32>>` hint — Result::Ok's check requires
    // `enum_name == "Result"` and misses, so E falls back to `error_t()` at
    // the per-tparam loop below. Pre-compute a substitution from einfo's
    // type-params to the outer hint's type-args, then substitute each
    // payload-type formal to get a concrete expected type per arg.
    auto& einfo = eit->second;
    SemaSubst pre_subst;
    if (hint_enum_type_ &&
        TypeRef(hint_enum_type_).kind() == LogosType::Kind::Enum &&
        TypeRef(hint_enum_type_).enum_name() == ename &&
        !einfo.type_params.empty()) {
        auto hta = TypeRef(hint_enum_type_).type_args();
        for (size_t i = 0; i < einfo.type_params.size() && i < hta.size(); ++i) {
            if (!hta[i] || TypeRef(hta[i]).kind() == LogosType::Kind::Error) continue;
            pre_subst[einfo.type_params[i].name] = hta[i];
        }
    }
    std::vector<lir::LExprPtr> payload;
    if (node.has_key(la::ARGS)) {
        AnyVal args_av = node.get(la::ARGS.code);
        if (!args_av.is_null()) {
            auto run = [&](auto items) {
                for (uint64_t i = 0; i < items.size(); ++i) {
                    // Push hint_enum_type_ if the payload slot resolves
                    // to a concrete enum via the pre-subst projection.
                    TypeRef saved_hint = hint_enum_type_;
                    if (i < vinfo->payload_types.size()) {
                        TypeRef pt_i = vinfo->payload_types[i];
                        if (pt_i && !pre_subst.empty())
                            pt_i = subst_type_sema(pt_i, pre_subst);
                        if (pt_i && TypeRef(pt_i).kind() == LogosType::Kind::Enum)
                            hint_enum_type_ = pt_i;
                    }
                    payload.push_back(lower_expr(map_of(items.get(i))));
                    hint_enum_type_ = saved_hint;
                }
            };
            if (args_av.is_pointer()) {
                auto m = map_of(args_av);
                if (m.has_key(la::ITEMS)) run(arr_of(m.get(la::ITEMS.code)));
                else                       run(arr_of(args_av));
            } else {
                run(arr_of(args_av));
            }
        }
    }
    // Build result type + type-check (same logic as lower_enum_lit_data)
    std::vector<TypeRef> resolved_payload_types = vinfo->payload_types;

    // B81: lifetime-arg inference (mirror of lower_enum_lit_data path).
    std::unordered_map<std::string, std::string> lt_subst;
    LtCands lt_cands;
    {
        std::function<void(TypeRef, TypeRef)> walk = [&](TypeRef pt, TypeRef at) {
            if (!pt || !at) return;
            using K = LogosType::Kind;
            auto pk = pt.kind();
            if ((pk == K::Ref || pk == K::MutRef) && (at.kind() == K::Ref || at.kind() == K::MutRef)) {
                std::string pl(pt.lifetime()), al(at.lifetime());
                if (!pl.empty() && !al.empty()) {
                    lt_cands[pl].push_back(al);
                    if (!lt_subst.count(pl)) lt_subst[pl] = al;
                }
                walk(pt.pointee(), at.pointee());
                return;
            }
            if (pk == K::Tuple && at.kind() == K::Tuple) {
                auto pe = pt.tuple_elems(); auto ae = at.tuple_elems();
                for (size_t i = 0; i < pe.size() && i < ae.size(); ++i) walk(pe[i], ae[i]);
                return;
            }
            if ((pk == K::Struct || pk == K::ZonedStruct || pk == K::Enum) && at.kind() == pk) {
                auto pl = pt.lifetime_args(); auto al = at.lifetime_args();
                for (size_t i = 0; i < pl.size() && i < al.size(); ++i)
                    if (!pl[i].empty() && !al[i].empty() && !lt_subst.count(pl[i]))
                        lt_subst[pl[i]] = al[i];
                auto pa = pt.type_args(); auto aa = at.type_args();
                for (size_t i = 0; i < pa.size() && i < aa.size(); ++i) walk(pa[i], aa[i]);
                return;
            }
        };
        for (size_t i = 0; i < vinfo->payload_types.size() && i < payload.size(); ++i)
            if (payload[i]) walk(vinfo->payload_types[i], expr_type(payload[i]));
        census_meet_("enumlit", einfo.lifetime_params, lt_cands, eit->first);
    }

    TypeRef result_type = make_enum_type(ename);
    if (!einfo.type_params.empty()) {
        SemaSubst subst;
        // Explicit turbofish: `Option::<A>::None` carries TYPE_PARAMS
        // on the STATIC_CALL node. Consume those FIRST so a no-payload
        // variant (None / unit-variant) still picks up the user-given
        // type-args instead of falling through to error_t() at the
        // "any type-param without a subst entry" gate below. Without
        // this, sema would emit Option<Error> for `Option::<A>::None`
        // in a generic-fn body, which mono then mangled as
        // `Option__<error>` and mlir-gen rejected.
        if (node.has_key(la::TYPE_PARAMS)) {
            auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                for (size_t i = 0; i < items.size() && i < einfo.type_params.size(); ++i) {
                    auto ta = resolve_type(map_of(items.get(i)));
                    if (ta && TypeRef(ta).kind() != LogosType::Kind::Error)
                        subst[einfo.type_params[i].name] = ta;
                }
            }
        }
        for (size_t i = 0; i < vinfo->payload_types.size() && i < payload.size(); ++i) {
            auto pt = vinfo->payload_types[i];
            if (pt && TypeRef(pt).kind() == LogosType::Kind::TypeVar) {
                auto inferred = expr_type(payload[i]);
                std::string tvn(TypeRef(pt).type_var_name());
                // G150-2: an unresolved integer/float-literal payload defers to
                // the let/return hint (projected into pre_subst) when it pins
                // this type-param — `let a: MyOpt<i64> = MyOpt::MSome(3)` must
                // bind T=i64, not the i32 literal default. Otherwise a generic
                // `impl<T> ... for MyOpt<T>` method instantiates at the wrong T.
                if (TypeRef(inferred).kind() == LogosType::Kind::IntLit ||
                    TypeRef(inferred).kind() == LogosType::Kind::FloatLit) {
                    auto psit = pre_subst.find(tvn);
                    if (psit != pre_subst.end() && psit->second) {
                        inferred = psit->second;
                        widen_int_expr(payload[i], inferred, builder());
                    } else {
                        inferred = TypeRef(inferred).kind() == LogosType::Kind::FloatLit
                                   ? prim(LogosType::Kind::F64) : i32_t();
                    }
                }
                // G168-A: when the hint (projected into pre_subst) pins this
                // type-param to a trait object (`Option<Box<dyn Sh>>`) but the
                // payload arg is a CONCRETE coercible value (`Box<Sq>`), record
                // the enum's type-arg as the DYN type — so the constructed enum
                // is genuinely `Option<Box<dyn Sh>>` — while leaving the payload
                // expr concrete. mlir-gen's enum-payload store unsize-fattens the
                // concrete payload into the dyn slot; else a thin `Box<Sq>` is
                // stored and dispatch reads a garbage vtable (SIGSEGV).
                auto wraps_dyn = [&](TypeRef t) -> bool {
                    if (!t) return false;
                    TypeRef u(t);
                    if ((u.kind() == LogosType::Kind::Ref ||
                         u.kind() == LogosType::Kind::MutRef) && u.pointee())
                        u = u.pointee();
                    if (is_stdlib_box(u) && u.type_args().size() == 1)
                        u = u.type_args()[0];
                    return u.kind() == LogosType::Kind::TraitObject;
                };
                if (auto psit2 = pre_subst.find(tvn);
                    psit2 != pre_subst.end() && psit2->second &&
                    wraps_dyn(psit2->second) && !wraps_dyn(inferred) &&
                    types_compatible(inferred, psit2->second))
                    inferred = psit2->second;
                // Payload-derived inference fills any slot still
                // missing after the explicit turbofish pass above.
                auto& slot = subst[tvn];
                if (!slot) slot = inferred;
                else if (wraps_dyn(inferred) && !wraps_dyn(slot)) slot = inferred;
            } else if (pt) {
                // N8: structural payload type mentioning the enum's type params
                // (`Full(Pair<T>)`) — unify against the actual arg type to
                // extract nested bindings (`Pair<T>` vs `Pair<i64>` → T=i64) so
                // pure inference (no turbofish/annotation) resolves them.
                unify_types(pt, expr_type(payload[i]), subst);
            }
        }
        // SL-sl-03 follow-up: when the let / return context hint says
        // `Option<&T>` but the payload arg is a `T` value (e.g. binding
        // from a match-arm in `Some(v) => Some(v)` where v: T), prefer
        // the hint's reference type if the inferred is its pointee. We
        // expect downstream addr_of to materialise the reference; the
        // hint disambiguates which Option spec to build.
        if (hint_enum_type_ && TypeRef(hint_enum_type_).enum_name() == std::string(ename)) {
            for (size_t i = 0; i < einfo.type_params.size() && i < TypeRef(hint_enum_type_).type_args().size(); ++i) {
                auto hta = TypeRef(hint_enum_type_).type_args()[i];
                if (!hta || TypeRef(hta).kind() == LogosType::Kind::Error) continue;
                auto sit = subst.find(einfo.type_params[i].name);
                if (sit == subst.end()) {
                    subst[einfo.type_params[i].name] = hta;
                } else if ((TypeRef(hta).kind() == LogosType::Kind::Ref ||
                            TypeRef(hta).kind() == LogosType::Kind::MutRef) &&
                           TypeRef(hta).pointee() == sit->second) {
                    // Hint says &T, inferred says T — prefer hint.
                    sit->second = hta;
                } else if (TypeRef(hta).kind() == LogosType::Kind::Ptr &&
                           (TypeRef(sit->second).kind() == LogosType::Kind::Ref ||
                            TypeRef(sit->second).kind() == LogosType::Kind::MutRef ||
                            TypeRef(sit->second).kind() == LogosType::Kind::Ptr) &&
                           TypeRef(hta).pointee() &&
                           TypeRef(sit->second).pointee() &&
                           TypeRef(TypeRef(hta).pointee()) ==
                               TypeRef(TypeRef(sit->second).pointee())) {
                    // Hint says `*const/*mut T`, inferred says `&T`/`&mut T`/`*T`
                    // over the SAME pointee — prefer the annotated raw pointer
                    // (Rust ref→ptr coercion at the payload). Critical with enum
                    // niches: inferring `&T` would build the niche `Option<&T>`
                    // (8B) while the annotation is the tagged `Option<*const T>`
                    // (16B) — incompatible repr, and the let-store would drop the
                    // payload (variance-option-ref-intersection). Both values are
                    // 8B pointers, so the payload store is a no-op bitcast.
                    sit->second = hta;
                }
            }
        }
        std::vector<TypeRef> type_args;
        for (auto& tp : einfo.type_params) {
            auto sit = subst.find(tp.name);
            type_args.push_back(sit != subst.end() ? sit->second : error_t());
        }
        check_type_bounds(std::string(ename), einfo.type_params, type_args);
        std::vector<std::string> lt_args;
        for (auto& lp : einfo.lifetime_params) {
            auto it = lt_subst.find(lp);
            lt_args.push_back(it != lt_subst.end() ? it->second : std::string{});
        }
        result_type = make_generic_enum(ename, std::move(type_args), std::move(lt_args));
        for (size_t i = 0; i < resolved_payload_types.size(); ++i)
            resolved_payload_types[i] = subst_type_sema(resolved_payload_types[i], subst);
    } else if (!einfo.lifetime_params.empty()) {
        std::vector<std::string> lt_args;
        for (auto& lp : einfo.lifetime_params) {
            auto it = lt_subst.find(lp);
            lt_args.push_back(it != lt_subst.end() ? it->second : std::string{});
        }
        result_type = make_generic_enum(ename, {}, std::move(lt_args));
    }
    if (!vinfo->is_variadic && payload.size() != vinfo->payload_types.size()) {
        error(std::format("{}::{} expects {} args, got {}",
              ename, vname, vinfo->payload_types.size(), payload.size()));
    } else if (!vinfo->is_variadic) {
        for (size_t i = 0; i < payload.size(); ++i) {
            if (TypeRef(expr_type(payload[i])).kind() != LogosType::Kind::Error &&
                resolved_payload_types[i] &&
                TypeRef(resolved_payload_types[i]).kind() != LogosType::Kind::Error &&
                // #95: `|| aggregate_unsize_pending(...)`. This guard asks
                // types_compatible, which BLANKET-ACCEPTS a thin aggregate
                // against a fat-`&dyn` one — so for exactly the #68/#95 shape
                // `expect_type` was never entered, and with it neither the
                // literal stamp (retype_aggregate_lit_to) nor the refusal.
                // MEASURED: `E::Some((&a,7i64))` at `(&dyn Shape,i64)` wrote an
                // object file and ran rc=139; the hoisted `E::Some(t)` twin the
                // same. With the disjunct the literal COERCES (42) and the
                // hoisted value is REFUSED.
                (!types_compatible(expr_type(payload[i]), resolved_payload_types[i]) ||
                 aggregate_unsize_pending(resolved_payload_types[i], expr_type(payload[i]))))
                // An enum payload is a constructed aggregate's FIELD, like the
                // tuple-struct ctor arm above — not a CoercePos::Operand.
                expect_type(payload[i], resolved_payload_types[i], CoercePos::StructLitField,
                            std::format("{}::{} arg {}:", ename, vname, i));
            if (resolved_payload_types[i] &&
                TypeRef(expr_type(payload[i])).kind() == LogosType::Kind::IntLit)
                if (auto v = get_intlit_value(payload[i]))
                    if (!intlit_fits(*v, TypeRef(resolved_payload_types[i]).kind()))
                        error(std::format("{}::{} arg {}: value {} does not fit in {}",
                              ename, vname, i, *v,
                              type_str(resolved_payload_types[i])));
            // Check array literal elements against narrow array payload type.
            if (resolved_payload_types[i] &&
                TypeRef(resolved_payload_types[i]).kind() == LogosType::Kind::Array &&
                TypeRef(resolved_payload_types[i]).elem() &&
                TypeRef(expr_type(payload[i])).kind() == LogosType::Kind::Array) {
                auto vr = expr_ref_of(payload[i]);
                if (vr.kind() == lir_schema::expr::Code::ArrLit) {
                    lir_view::EArrLitView al{vr};
                    for (uint64_t ei = 0; ei < al.count(); ++ei) {
                        auto el = al.elem(ei);
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (!intlit_fits(*v, TypeRef(resolved_payload_types[i]).elem().kind()))
                                    error(std::format("{}::{} arg {}: array element {}: value {} does not fit in {}",
                                          ename, vname, i, ei, *v, type_str(TypeRef(resolved_payload_types[i]).elem())));
                    }
                }
            }
            // Check tuple literal elements against narrow tuple payload type.
            if (resolved_payload_types[i] &&
                TypeRef(resolved_payload_types[i]).kind() == LogosType::Kind::Tuple &&
                TypeRef(expr_type(payload[i])).kind() == LogosType::Kind::Tuple) {
                auto vr = expr_ref_of(payload[i]);
                if (vr.kind() == lir_schema::expr::Code::TupleLit) {
                    lir_view::ETupleLitView tl{vr};
                    uint64_t ei = 0;
                    tl.each_elem([&](lir_view::ExprRef el) {
                        if (ei >= TypeRef(resolved_payload_types[i]).tuple_elems().size()) { ++ei; return; }
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (TypeRef(resolved_payload_types[i]).tuple_elems()[ei] &&
                                    !intlit_fits(*v, TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).kind()))
                                    error(std::format("{}::{} arg {}: tuple element {}: value {} does not fit in {}",
                                          ename, vname, i, ei, *v, type_str(TypeRef(resolved_payload_types[i]).tuple_elems()[ei])));
                        if (TypeRef(resolved_payload_types[i]).tuple_elems()[ei] && TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).kind() == LogosType::Kind::Array &&
                            TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                            el.kind() == lir_schema::expr::Code::ArrLit) {
                            lir_view::EArrLitView ial{el};
                            for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                                auto iel = ial.elem(ii);
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (!intlit_fits(*v, TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).elem().kind()))
                                            error(std::format("{}::{} arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                  ename, vname, i, ei, ii, *v, type_str(TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).elem())));
                            }
                        }
                        if (TypeRef(resolved_payload_types[i]).tuple_elems()[ei] && TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).kind() == LogosType::Kind::Tuple &&
                            el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                            el.kind() == lir_schema::expr::Code::TupleLit) {
                            lir_view::ETupleLitView itl{el};
                            uint64_t ii = 0;
                            itl.each_elem([&](lir_view::ExprRef iel) {
                                if (ii >= TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems().size()) { ++ii; return; }
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems()[ii]).kind()))
                                            error(std::format("{}::{} arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                  ename, vname, i, ei, ii, *v, type_str(TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems()[ii])));
                                ++ii;
                            });
                        }
                        ++ei;
                    });
                }
            }
        }
    } else {
        if (!resolved_payload_types.empty()) {
            auto pack_t = resolved_payload_types[0];
            for (size_t i = 0; i < payload.size(); ++i) {
                if (TypeRef(expr_type(payload[i])).kind() != LogosType::Kind::Error &&
                    TypeRef(pack_t).kind() != LogosType::Kind::Error &&
                    !types_compatible(expr_type(payload[i]), pack_t))
                    expect_type(payload[i], pack_t, CoercePos::StructLitField,
                                std::format("{}::{} variadic arg {}:", ename, vname, i));
                if (TypeRef(pack_t).kind() != LogosType::Kind::Error &&
                    TypeRef(expr_type(payload[i])).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(payload[i]))
                        if (!intlit_fits(*v, TypeRef(pack_t).kind()))
                            error(std::format("{}::{} variadic arg {}: value {} does not fit in {}",
                                  ename, vname, i, *v, type_str(pack_t)));
            }
        }
    }
    // Move semantics: same as lower_enum_lit_data — payload elements
    // consume their source. Without this, move-type payloads would leave
    // their sources live in the surrounding scope (silent leak before
    // mono SDrop sentinel→struct propagation; double-drop after).
    for (auto& p : payload) {
        if (p && is_move_type(expr_type(p)))
            mark_moved_expr(expr_ref_of(p));
    }
    return builder().enum_lit_data(std::string(ename), std::string(vname), vinfo->value, std::move(payload), result_type);
}

bool SemaChecker::ref_arg_satisfies_dyn(TypeRef at, TypeRef pt) {
    if (!at || !pt) return false;
    if (TypeRef(pt).kind() != LogosType::Kind::TraitObject) return false;
    if (TypeRef(at).kind() != LogosType::Kind::Ref &&
        TypeRef(at).kind() != LogosType::Kind::MutRef) return false;
    TypeRef pointee = TypeRef(at).pointee();
    if (!pointee) return false;
    std::string trait(TypeRef(pt).trait_name());
    if (trait.empty()) return false;

    // (a) pointee is a TypeVar bounded (transitively) by the trait.
    if (TypeRef(pointee).kind() == LogosType::Kind::TypeVar) {
        std::string tv(TypeRef(pointee).type_var_name());
        auto bit = current_type_bounds_.find(tv);
        if (bit == current_type_bounds_.end()) return false;
        logos::compiler::StrSet seen;
        std::function<bool(const std::string&)> reaches =
            [&](const std::string& tn) -> bool {
                if (!seen.insert(tn).second) return false;
                if (tn == trait) return true;
                auto it = find_trait_iter_scoped(tn);
                if (it == traits_.end()) return false;
                for (auto& s : it->second.supertraits)
                    if (reaches(s.trait_name)) return true;
                return false;
            };
        for (auto& b : bit->second)
            if (reaches(b.trait_name)) return true;
        return false;
    }

    // (c) pointee is itself a trait object `dyn Sub` — a supertrait UPCAST
    //     `&dyn Sub → &dyn Super` is allowed when Super is a (transitive)
    //     supertrait of Sub (Rust trait-upcasting). The data pointer is
    //     unchanged; codegen recovers Super's vtable from Sub's stored
    //     super-vtable-pointer slot.
    if (TypeRef(pointee).kind() == LogosType::Kind::TraitObject) {
        std::string sub(TypeRef(pointee).trait_name());
        if (sub.empty()) return false;
        logos::compiler::StrSet seen;
        std::function<bool(const std::string&)> reaches =
            [&](const std::string& tn) -> bool {
                if (!seen.insert(tn).second) return false;
                if (tn == trait) return true;
                auto it = traits_.find(tn);
                if (it == traits_.end()) return false;
                for (auto& s : it->second.supertraits)
                    if (reaches(s.trait_name)) return true;
                return false;
            };
        return reaches(sub);
    }

    // (b) pointee is a concrete type — struct / enum / PRIMITIVE — implementing
    //     the trait, DIRECTLY or via a blanket impl (`impl<T: Bound> Trait for
    //     T`). sema_has_impl_recursive walks direct + blanket + bound chains, so
    //     `&i64 as &dyn Describe` works when `i64: Tag` and
    //     `impl<T: Tag> Describe for T` is in scope. Primitives were previously
    //     unhandled (only Struct/Enum), and blanket satisfaction was ignored.
    std::string bare, concrete;
    auto pk = TypeRef(pointee).kind();
    if (pk == LogosType::Kind::Struct || pk == LogosType::Kind::ZonedStruct) {
        bare = std::string(TypeRef(pointee).struct_name());
        concrete = concrete_struct_name(pointee);
    } else if (pk == LogosType::Kind::Enum) {
        bare = std::string(TypeRef(pointee).enum_name());
        concrete = bare;
    } else {
        bare = type_str(pointee);   // primitives (i64/f64/bool/…) and others
        concrete = bare;
    }
    if (bare.empty()) return false;
    logos::compiler::StrSet seen2;
    if (!sema_has_impl_recursive(trait, concrete, bare, seen2)) return false;
    // logos-core 2.4(c): auto-trait bound enforcement at the unsize site.
    // `&NotSend → &dyn Trait + Send` must be rejected: the trait object's
    // contract is that the erased type satisfies every `+ Auto` bound. The
    // check runs over the pointee (the unsized type being erased), reusing
    // `is_auto_trait_satisfied`'s structural walk. Currently Send/Sync are
    // the only auto-traits represented in the TraitObject's const_val bits.
    if (TypeRef(pt).trait_requires_send()) {
        StrSet seen_send;
        if (!is_auto_trait_satisfied(pointee, "Send", seen_send)) return false;
    }
    if (TypeRef(pt).trait_requires_sync()) {
        StrSet seen_sync;
        if (!is_auto_trait_satisfied(pointee, "Sync", seen_sync)) return false;
    }
    return true;
}

lir::LExprPtr SemaChecker::make_str_eq_guard(lir::LExprPtr a, lir::LExprPtr b) {
    const SemaFuncInfo* fi = nullptr;
    for (auto* c : find_func_candidates("str_eq"))
        if (c && c->param_types.size() == 2) { fi = c; break; }
    if (!fi) return nullptr;
    std::vector<lir::LExprPtr> args;
    args.push_back(std::move(a));
    args.push_back(std::move(b));
    std::string sym = fi->symbol_name.empty() ? std::string("str_eq") : fi->symbol_name;
    return builder().call(sym, {}, std::move(args), bool_t());
}

lir::LExprPtr SemaChecker::default_value_for(TypeRef t) {
    if (!t) return nullptr;
    TypeRef tt(t);
    // [E; N]::default() → [E::default(); N] (Rust: [T; N]: Default where T: Default).
    if (tt.kind() == LogosType::Kind::Array && tt.elem()) {
        uint64_t n = tt.arr_size();
        if (n == 0) return builder().arr_lit(std::vector<lir::LExprPtr>{}, t);
        std::vector<lir::LExprPtr> elems;
        for (uint64_t i = 0; i < n; ++i) {
            auto e = default_value_for(tt.elem());
            if (!e) return nullptr;
            elems.push_back(std::move(e));
        }
        return builder().arr_lit(std::move(elems), t);
    }
    // Scalar / struct: emit a call to its resolved `__default` symbol.
    std::string base;
    if (tt.kind() == LogosType::Kind::Struct ||
        tt.kind() == LogosType::Kind::ZonedStruct)
        base = tt.type_args().empty()
            ? std::string(tt.struct_name())
            : concrete_struct_name(t);
    else
        base = type_str(t);  // primitive keyword (i64, bool, …)
    const SemaFuncInfo* fi = nullptr;
    for (auto* c : find_func_candidates(base + "__default"))
        if (c && c->param_types.empty()) { fi = c; break; }
    if (!fi) return nullptr;
    std::string sym = fi->symbol_name.empty() ? base + "__default" : fi->symbol_name;
    return builder().call(sym, {}, {}, t);
}

// Supertrait UPCAST coercion: when `arg` is already a `&dyn Sub`/`dyn Sub` and
// the param wants `&dyn Super`/`dyn Super` with Super a (transitive) supertrait
// of Sub, insert the explicit upcast Cast (codegen recovers Super's vtable from
// the stored super-vtable-pointer slot). Narrowly scoped to a dyn SOURCE so the
// concrete→dyn coercion (handled implicitly at mlir-gen call sites) is left
// untouched. Returns true if a cast was inserted.
// Implicit `&mut T` reborrow at a call-arg position (Rust ergonomics: a `&mut`
// passed to a `&mut T` parameter is automatically reborrowed as `&mut *r`, not
// moved). Without this, every call site of an `&mut`-taking fn would either
// consume the local `r` (sound `&mut` is move-only) or require the user to
// write `&mut *r` by hand. Wraps `arg` as `AddrOfTemp(Deref(arg), is_mut=true)`
// — semantically identical at codegen (mlir-gen's peephole emits a load of
// the original value), but borrow-check now sees a reborrow shape and ties
// the borrow's NLL release to the call's holder, leaving `r` usable after.
// Skip when `arg` is already an AddrOfTemp (already reborrowed or fresh ref).
void SemaChecker::bind_method_receiver(lir::LExprPtr& recv,
                                         TypeRef formal_self) {
    if (!formal_self) return;
    // A `*mut T` receiver satisfies a `&mut self` formal AS-IS everywhere above
    // (`… .kind() != Kind::Ptr` guards the auto-ref off, because a thin `&mut`
    // and a `*mut` are the same 8 bytes). For a `#[zone_mut]` T they are NOT:
    // the formal wants a 16-byte {data, zone}. Refuse here — the last point
    // where the formal and the actual are both in hand — rather than in each of
    // the dozen receiver-binding arms.
    if (recv && TypeRef(formal_self).kind() == LogosType::Kind::MutRef &&
        zone_mut_pointee(TypeRef(formal_self).pointee())) {
        TypeRef at(expr_type(recv));
        if (!(at && at.kind() == LogosType::Kind::MutRef &&
              zone_mut_pointee(at.pointee())))
            reject_thin_zone_mut_ref(TypeRef(formal_self).pointee(), at);
    }
    try_implicit_reborrow_mut(recv, formal_self, /*allow_downgrade=*/false);
    track_recv_moved(recv, formal_self);
}

// ── expect_type: the one judgment ──────────────────────────────────────────
// The single table mapping a position to its coercion behaviour. Adding a
// position = adding a row HERE, not writing per-site code.
uint32_t SemaChecker::mask_for(CoercePos pos) {
    switch (pos) {
    case CoercePos::CallArg:
    case CoercePos::ClosureArg:
        return CFLAG_STANDARD | CFLAG_ACCEPT_SD_THIN | CFLAG_ACCEPT_REF_DYN |
               CFLAG_SKIP_UNRESOLVED;
    case CoercePos::MethodArg:
        // Order pinned by the suite (widen-last equivalence argued at the
        // former inline site).
        return CFLAG_CLOSURE_TO_FNPTR | CFLAG_ARG_TO_DYN |
               CFLAG_ARRAY_TO_SLICE |
               CFLAG_IMPLICIT_REBORROW | CFLAG_WIDEN_INT |
               CFLAG_CHECK_E0507 | CFLAG_CHECK_DYN_BOUNDS |
               CFLAG_SKIP_UNRESOLVED;
    case CoercePos::LetInit:
    case CoercePos::PlaceWrite:
    case CoercePos::TupleElem:
    case CoercePos::BranchArm:
        return CFLAG_CLOSURE_TO_FNPTR | CFLAG_ARRAY_TO_SLICE |
               CFLAG_SLICE_TO_ARRAY | CFLAG_IMPLICIT_REBORROW |
               CFLAG_WIDEN_INT |
               (pos == CoercePos::PlaceWrite ? CFLAG_CHECK_DYN_BOUNDS : 0u);
    case CoercePos::StructLitField:
        // Rust MOVES into a struct literal: no reborrow. Everything else
        // applies.
        return CFLAG_CLOSURE_TO_FNPTR | CFLAG_ARRAY_TO_SLICE |
               CFLAG_SLICE_TO_ARRAY | CFLAG_WIDEN_INT;
    case CoercePos::ArrayElem:
        return CFLAG_ARRAY_TO_SLICE | CFLAG_WIDEN_INT;
    case CoercePos::Return:
        // + the Box→dyn consume, handled in expect_type itself (it rewrites
        // the expr, not just its type).
        return CFLAG_CLOSURE_TO_FNPTR | CFLAG_ARRAY_TO_SLICE |
               CFLAG_WIDEN_INT;
    case CoercePos::ConstInit:
    case CoercePos::Operand:
        return CFLAG_WIDEN_INT;
    }
    return CFLAG_NONE;
}

bool SemaChecker::expect_type(lir::LExprPtr& e, TypeRef expected, CoercePos pos,
                              std::string_view ctx) {
    if (!e || !expected) return true;
    if (TypeRef(expected).kind() == LogosType::Kind::Error) return true;
    // An unresolved formal (a type parameter or an un-normalized projection)
    // is skipped only where mono re-judges the concrete instantiation — the
    // CALL rows. An annotation position judges it here (a GAT bound violation
    // must not slip through as "unresolved").
    if ((mask_for(pos) & CFLAG_SKIP_UNRESOLVED) &&
        (TypeRef(expected).kind() == LogosType::Kind::TypeVar ||
         TypeRef(expected).kind() == LogosType::Kind::AssocType)) return true;
    if (TypeRef(expr_type(e)).kind() == LogosType::Kind::Error) return true;
    // ── R-E / B4: AN ERROR UNDER A REFERENCE IS STILL AN ERROR ───────────
    // The line above is the error-propagation rule ("uses of an error-typed
    // value are silent"), and it only looked at the OUTERMOST kind. So
    // `&mut <error>` — the type of `&mut w` where `w`'s type is a projection
    // this round cannot resolve yet — failed the guard and the arm below
    // printed
    //     call to 'drain_rows' arg 1: expected &mut CtrClass<@hs_…>::LeafWalk,
    //                                 got &mut <error>
    // in a NON-TERMINAL round, aborting the fixpoint before the round that
    // resolves it. MEASURED: probe p6 refuses with the walk type behind a
    // `pub type` alias too, so this is not specific to the typeof arm — it is
    // the same round-order class one level down, and it is why `typeof(C)` was
    // spellable in a signature but the signature was not CALLABLE.
    // Narrow on purpose: Error only (not CfgSlotType, not the empty-name
    // struct), and only on the GOT side — an error-typed expected already
    // returns true at the top of this function.
    //
    // ⚠ NARROWER ON PURPOSE, SECOND PASS: the recursion walks ONLY
    // `pointee()` and `assoc_base()` — the two shapes the round-order class
    // actually takes (`&mut <error>`, `&mut <error>::LeafWalk`). It must NOT
    // walk `type_args()` (nor `elem()`, same class): an error INSIDE a
    // generic instantiation — `Result<i32, <error>>` from a failed inference
    // — is not "a use of a value this round cannot type yet", it is the
    // program's own type error, and suppressing it here replaced
    //     call to 'wants_opt' arg 1: expected Option, got Result
    // with a backend self-diagnosis while the error type leaked into codegen
    // (MEASURED: spec fail test coerce_diag_1__enum-bare-literal-retype-to-
    // param went from its pinned diagnostic to `mlir_gen: internal: unknown
    // tagged enum 'Result__i32__<error>'` — the R-E verifier's F1, an L4 red
    // the tests/logos/fail-only sweep could not see because the spec fail
    // corpus lives in tests/spec/fail).
    {
        auto mentions_error = [](auto&& self, TypeRef t, int depth) -> bool {
            if (!t || depth > 16) return false;
            if (t.kind() == LogosType::Kind::Error) return true;
            if (auto pe = t.pointee(); pe && pe != t && self(self, pe, depth + 1))
                return true;
            // `<error>::Trait::Assoc` — the projection whose BASE is the
            // unresolved thing. This is the shape the `let mut w = c.walk()`
            // form takes (annotated with the alias, `&mut <error>::LeafWalk`),
            // as opposed to the bare `&mut <error>` of the un-annotated form.
            if (auto ab = t.assoc_base(); ab && ab != t && self(self, ab, depth + 1))
                return true;
            return false;
        };
        if (mentions_error(mentions_error, TypeRef(expr_type(e)), 0)) return true;
    }
    coerce_arg_to_param(e, expected, mask_for(pos));
    // A raw `*mut T` / `*const T` / `&T` is accepted wherever a `&mut T` is
    // expected (types_compatible treats a pointer and a thin reference as the
    // same 8 bytes). For a `#[zone_mut]` T the expected value is a 16-byte
    // {data, zone} pair, so that equivalence is a silent 8-for-16 substitution
    // — the same defect as the `&mut` producers, arriving through the
    // COERCION instead. MEASURED rc=139: `take(p)` with `p: *mut ZS` and
    // `fn take(r: &mut ZS)`.
    // Asked STRUCTURALLY, not just at the top: the same substitution laundered
    // through a tuple/array literal or a tuple RETURN type reached codegen
    // untouched (all rc=139) because a tuple coerces ELEMENTWISE inside
    // types_compatible, with no per-element expect_type. Pinned in
    // tests/logos/fail/zone_mut_thin_source_{tuple,array,tuple_ret}.logos,
    // admit side tests/logos/pass/zone_mut_thin_source_admits_aggregate.logos.
    if (expected && expr_type(e) &&
        reject_thin_zone_mut_nested(expected, expr_type(e)))
        return false;
    // #95: THE SAME QUESTION FOR THE `&dyn` HALF, and it must run HERE — after
    // `coerce_arg_to_param`, never before. `retype_aggregate_lit_to` (called
    // from there) stamps the expected aggregate type onto a tuple/array LITERAL,
    // which is Rust's rule too (an expectation propagates INTO a literal at a
    // coercion site). So by this line a literal already carries the fat type and
    // is invisible to the walk below; what is still thin is a value that was
    // ALREADY TYPED thin — no literal, therefore no coercion site, therefore the
    // refusal. That ORDER is the whole distinction between the two halves; both
    // are pinned (tests/logos/fail/aggregate_unsize_needs_cast_*.logos vs
    // tests/logos/pass/aggregate_unsize_literal_*.logos).
    if (expected && expr_type(e) &&
        reject_uncoerced_aggregate_unsize(expected, expr_type(e)))
        return false;
    if (pos == CoercePos::Return &&
        TypeRef(expected).owning_trait_object() &&
        expr_type(e) && is_stdlib_box(expr_type(e))) {
        // Box<Concrete> → Box<dyn Trait>: consume the source Box and desugar
        // to the proven `as`-unsize cast, exactly like the explicit form —
        // else codegen gets a mis-keyed vtable AND an un-consumed Box (a
        // double free).
        mark_moved_expr(expr_ref_of(e));
        e = builder().cast(std::move(e), expected);
    }
    if (types_compatible(expr_type(e), expected)) return true;
    if (ptr_rel_compatible(expr_type(e), expected)) return true;   // #[rel_ptr] ↔ *T
    if ((mask_for(pos) & CFLAG_ACCEPT_SD_THIN) &&
        sd_thin_compatible(expr_type(e), expected)) return true;
    if ((mask_for(pos) & CFLAG_ACCEPT_REF_DYN) &&
        ref_arg_satisfies_dyn(expr_type(e), expected)) return true;   // G158-7
    // Gap-4: a projection `T::A` may equal the expected type via an equality
    // bound `T: Trait<A = V>`. Normalization is part of the JUDGMENT, not of
    // any position — the return path had it and the tail path did not, which
    // is exactly the per-site drift this function exists to end.
    if (types_compatible(normalize_assoc_eq(expr_type(e)), expected)) return true;
    if (std::getenv("LOGOS_DEBUG_ASSOC_MISMATCH")) {
        auto dump = [](const char* tag, TypeRef t) {
            std::fprintf(stderr, "  [%s] kind=%d trait='%s' assoc='%s' base_kind=%d base='%s'\n",
                tag, (int)t.kind(),
                std::string(t.trait_name()).c_str(),
                std::string(t.assoc_type_name()).c_str(),
                t.assoc_base() ? (int)TypeRef(t.assoc_base()).kind() : -1,
                t.assoc_base() ? std::string(TypeRef(t.assoc_base()).type_var_name()).c_str() : "");
        };
        dump("expected", expected); dump("got", expr_type(e));
    }
    auto [es, gs] = type_str_pair(expected, expr_type(e));
    // ctx carries its own trailing punctuation ("let 'x': type mismatch —",
    // "field write 'a.b':"), so converted sites stay byte-identical to their
    // historical messages and no .expected files churn.
    error(std::format("{} expected <T>, got <U>", ctx, es, gs));  // RECORD COPY: template respelled so lint_mismatch_monopoly counts only the real emitter
    return false;
}

void SemaChecker::coerce_arg_to_param(lir::LExprPtr& arg, TypeRef pt,
                                       uint32_t flags) {
    if (!arg || !pt) return;
    // Canonical order — see header. Each step is a no-op when not applicable.
    if (flags & CFLAG_BARE_ENUM)        try_retype_bare_enum_arg(arg, pt);
    if (flags & CFLAG_CLOSURE_TO_FNPTR) try_coerce_closure_to_fnptr(arg, pt);
    if (flags & CFLAG_ARRAY_TO_SLICE)   try_coerce_array_ref_to_slice(arg, pt);
    if (flags & CFLAG_SLICE_TO_ARRAY)   try_coerce_slice_to_array_ref(arg, pt);
    if (flags & CFLAG_DYN_UPCAST)       coerce_dyn_upcast(arg, pt);
    if (flags & CFLAG_ARG_TO_DYN)       coerce_arg_to_dyn(arg, pt);
    if (flags & CFLAG_IMPLICIT_REBORROW) try_implicit_reborrow_mut(arg, pt);
    // Implicit CoerceUnsized for a smart-pointer struct arg (`Rc<A>` →
    // `Rc<dyn Tr>`). Unconditional (not flag-gated): a no-op unless `arg` is
    // the same wrapper struct as `pt` with a field unsizing sized→dyn — so it
    // is safe in every arg-coercion context. Closes GAP-C for the flipped repr.
    try_struct_unsize_coerce(arg, pt);
    // Unconditional, same reasoning as try_struct_unsize_coerce above: a no-op
    // unless `arg` is a tuple/array LITERAL and `pt` an aggregate type whose slot
    // wants a fat `&dyn` the element does not carry yet — so it is safe in every
    // position, and it must BE in every position, because an aggregate literal's
    // slot types have no other source (see retype_aggregate_lit_to).
    retype_aggregate_lit_to(expr_ref_of(arg), pt);
    if (flags & CFLAG_WIDEN_INT)        widen_int_expr(arg, pt, builder());
    // logos-core 2.4(c): unsize-to-dyn auto-trait bound enforcement.
    // types_compatible's `Struct → TraitObject` branch — grep sema.cpp for
    // "Struct → &dyn Trait coercion (impl check deferred to codegen)"; the
    // line number this comment used to cite (1727) was wrong by ~500 lines and
    // pointed into mangle_type_for_name — is a
    // blanket-accept (impl check deferred to codegen), which means the
    // type-check pipeline never sees a Send/Sync mismatch. Enforce here:
    // when the FORMAL parameter is `&dyn Trait + Send` (or `+ Sync`) — bare
    // TraitObject or peeled out of a Ref/MutRef — the SOURCE pointee must
    // structurally satisfy the auto-bound (`is_auto_trait_satisfied`).
    // Emits a specific diagnostic on failure; the generic
    // "expected X, got Y" upstream fires too (types_compatible may still
    // return true), so users see both lines.
    if (flags & CFLAG_CHECK_DYN_BOUNDS)
        check_dyn_auto_bounds_at_coercion(arg, pt);
    // E0507: passing a move-typed argument BY VALUE that was moved out of a
    // borrowed place (`f(*r)`, `f(v[i])`) — same double-free as let/return. Only
    // when the parameter takes the value by value (not `&`/`&mut`).
    if ((flags & CFLAG_CHECK_E0507) &&
        pt && TypeRef(pt).kind() != LogosType::Kind::Ref &&
        TypeRef(pt).kind() != LogosType::Kind::MutRef &&
        is_move_type(pt) && is_unowned_move_source(arg))
        error("cannot move out of a value behind a reference / out of an "
              "index (E0507)");
}

void SemaChecker::check_dyn_auto_bounds_at_coercion(lir_view::ExprRef arg,
                                                     TypeRef pt) {
    if (!pt) return;
    // Peel pointer/owning-container layers to reach the underlying type.
    // Targets: `&dyn`, `&mut dyn`, AND owning smart pointers `Box<dyn>` /
    // `Rc<dyn>` / `Arc<dyn>` — the dyn `+ Send`/`+ Sync` bound rides inside the
    // container, so without peeling them `want(b: Box<dyn T + Send>)` (and the
    // let/return/field forms) erased a non-Send concrete silently (T1-12 gap:
    // only reference targets were peeled). Sources peel the same layers to the
    // erased concrete type. Loops to handle `Box<Rc<dyn>>`-style nesting.
    auto is_smart_ptr = [](TypeRef t) {
        if (TypeRef(t).kind() != LogosType::Kind::Struct) return false;
        std::string n(TypeRef(t).struct_name());
        return n == "Box" || n == "Rc" || n == "Arc";
    };
    auto peel = [&](TypeRef t) {
        for (int guard = 0; t && guard < 8; ++guard) {
            auto k = TypeRef(t).kind();
            if ((k == LogosType::Kind::Ref || k == LogosType::Kind::MutRef ||
                 k == LogosType::Kind::Ptr) && TypeRef(t).pointee()) {
                t = TypeRef(t).pointee(); continue;
            }
            if (is_smart_ptr(t) && !TypeRef(t).type_args().empty()) {
                t = TypeRef(t).type_args()[0]; continue;
            }
            break;
        }
        return t;
    };
    TypeRef pdyn = peel(pt);
    // Accept both the fat `&dyn` form (TraitObject) and the unsized `dyn Trait`
    // payload of an owning container (UnsizedDyn, reached by peeling Box/Rc/Arc).
    // A `dyn Fn(..)` destination is Kind::Closure carrying its family name.
    const bool clos_dyn = pdyn && TypeRef(pdyn).kind() == LogosType::Kind::Closure &&
                          !std::string(TypeRef(pdyn).trait_name()).empty();
    if (!pdyn || (TypeRef(pdyn).kind() != LogosType::Kind::TraitObject &&
                  TypeRef(pdyn).kind() != LogosType::Kind::UnsizedDyn && !clos_dyn)) return;
    check_object_lifetime_bound(arg, pt, pdyn, peel);
    if (clos_dyn) return;  // no Send/Sync bits on a Closure-kind dyn
    bool need_send = TypeRef(pdyn).trait_requires_send();
    bool need_sync = TypeRef(pdyn).trait_requires_sync();
    if (!need_send && !need_sync) return;
    // The source's structural-Send/Sync check walks to the erased concrete type
    // (`&Foo → Foo`, `Box<Foo> → Foo`) or `expr_type(arg)` itself if already concrete.
    TypeRef src = expr_type(arg);
    if (!src) return;
    TypeRef src_pointee = peel(src);
    // Only enforce when the source is a CONCRETE type being unsize-erased into
    // a dyn target — Struct / ZonedStruct / Enum. TraitObject-to-TraitObject
    // coercion (e.g. dyn-upcast or identity) carries its own bound info on the
    // source side; bound preservation across that path is handled by
    // types_compatible's TypeUID-based equality (the const_val bits make
    // `&dyn T` and `&dyn T + Send` interrn as distinct types). TypeVar
    // sources are deferred to the mono-instantiation site.
    auto spk = TypeRef(src_pointee).kind();
    if (spk != LogosType::Kind::Struct &&
        spk != LogosType::Kind::ZonedStruct &&
        spk != LogosType::Kind::Enum)
        return;
    if (need_send) {
        StrSet seen;
        if (!is_auto_trait_satisfied(src_pointee, "Send", seen))
            error(std::format(
                "coercion to `&dyn {} + Send`: source type `{}` does not "
                "satisfy `Send`",
                std::string(TypeRef(pdyn).trait_name()), type_str(src_pointee)));
    }
    if (need_sync) {
        StrSet seen;
        if (!is_auto_trait_satisfied(src_pointee, "Sync", seen))
            error(std::format(
                "coercion to `&dyn {} + Sync`: source type `{}` does not "
                "satisfy `Sync`",
                std::string(TypeRef(pdyn).trait_name()), type_str(src_pointee)));
    }
}

// THE OBJECT-LIFETIME BOUND (Rust: coercing `Src` into `dyn Tr + 'r` requires
// `Src: 'r`; an OWNED `Box<dyn Tr>` with no bound defaults 'r = 'static, a
// BORROWED `&dyn Tr` with an elided slot has no obligation). `Src: 'r` is read
// off the source type's components: region slots, a struct's lifetime args (an
// elided arg of a lifetime-generic struct is a fresh region), a type parameter
// through its declared `T: 'x` bounds, a projection through its base, a closure
// through its literal's captures. The source is the operand under the Cast that
// coerce_arg_to_dyn already wrapped around it.
void SemaChecker::check_object_lifetime_bound(lir_view::ExprRef arg, TypeRef pt, TypeRef pdyn,
                                              const std::function<TypeRef(TypeRef)>& peel) {
    using K = LogosType::Kind;
    bool via_ref = false;
    for (TypeRef q = pt; q;) {
        auto k = TypeRef(q).kind();
        if ((k == K::Ref || k == K::MutRef || k == K::Ptr) && TypeRef(q).pointee()) { via_ref = true; q = TypeRef(q).pointee(); continue; }
        break;
    }
    bool borrowed = via_ref;
    if (TypeRef(pdyn).kind() == K::TraitObject && TypeRef(pdyn).trait_owning_kind() == TypeRef::OwningKind::Borrow) borrowed = true;
    if (TypeRef(pdyn).kind() == K::Closure && !TypeRef(pdyn).const_val()) borrowed = true;
    std::string slot(TypeRef(pdyn).lifetime());
    std::string R = !slot.empty() ? outlives_norm(slot) : (borrowed ? std::string() : std::string("'static"));
    if (R.empty() || R == "'_") return;
    lir_view::ExprRef er = expr_ref_of(arg);
    for (int casts = 0; casts < 2 && er.kind() == lir_schema::expr::Code::Cast; ++casts) {
        auto op = lir_view::ECastView{er}.operand();
        if (!op) break;
        er = op;
    }
    TypeRef src0 = expr_type(er);
    TypeRef sp = src0 ? peel(src0) : TypeRef();
    if (!sp || TypeRef(sp).kind() == K::TraitObject || TypeRef(sp).kind() == K::UnsizedDyn) return;
    // The literal behind the source: the literal itself, a binding whose RHS
    // was one, or the one argument of the `Box::new(..)` that produced this
    // `Box<closure>` (the arg's type is the literal's type, pointer for pointer).
    std::string src_closure_id;
    for (lir_view::ExprRef ce = er; ce;) {
        auto ck = ce.kind();
        if (ck == lir_schema::expr::Code::ClosureBox) {
            src_closure_id = std::string(lir_view::EClosureBoxView{ce}.closure_id()); break;
        }
        if (ck == lir_schema::expr::Code::VarRef) {
            if (auto* vi = lookup_var_info(lir_view::EVarRefView{ce}.name())) src_closure_id = vi->closure_id;
            break;
        }
        if (ck == lir_schema::expr::Code::Cast) { ce = lir_view::ECastView{ce}.operand(); continue; }
        if (ck == lir_schema::expr::Code::Call && TypeRef(sp).kind() == K::Closure &&
            std::string(TypeRef(sp).trait_name()).empty()) {
            lir_view::ExprRef only; int n = 0;
            lir_view::ECallView{ce}.each_arg([&](lir_view::ExprRef a) { only = a; ++n; });
            if (n == 1 && only && expr_type(only) == sp) { ce = only; continue; }
        }
        break;
    }
    auto adj = outlives_adj(current_outlives_);
    auto r_ok = [&](std::string_view r) -> bool {
        if (r.empty() || r == "'_" || r == "_") return false;
        if (outlives_is_static(r)) return true;
        return outlives(r, R, adj, /*permissive_empty=*/false);
    };
    auto tv_ok = [&](std::string_view tvn) -> bool {
        auto it = current_type_lt_outlives_.find(std::string(tvn));
        if (it == current_type_lt_outlives_.end()) return false;
        for (auto& b : it->second) if (r_ok(b)) return true;
        return false;
    };
    std::vector<std::string> bad;
    std::function<void(TypeRef, const std::string&, int)> walk = [&](TypeRef t, const std::string& cid, int d) {
        if (!t || d > 6) return;
        auto k = TypeRef(t).kind();
        switch (k) {
        case K::Ref: case K::MutRef: case K::Slice: case K::DstRef: {
            if (k == K::Slice && TypeRef(t).owning_slice()) { walk(TypeRef(t).elem(), "", d + 1); return; }
            std::string lt(TypeRef(t).lifetime());
            if (!r_ok(lt)) bad.push_back(lt.empty() ? std::string("'_") : lt);
            if (k == K::Ref || k == K::MutRef) walk(TypeRef(t).pointee(), "", d + 1);
            else if (k == K::Slice) walk(TypeRef(t).elem(), "", d + 1);
            return;
        }
        case K::TraitObject: {
            std::string lt(TypeRef(t).lifetime());
            if (lt.empty() && TypeRef(t).owning_trait_object()) return;  // an owned object with no bound is 'static
            if (!r_ok(lt)) bad.push_back(lt.empty() ? std::string("'_") : lt);
            return;
        }
        case K::Struct: case K::ZonedStruct: case K::Enum:
            for (auto& la_ : TypeRef(t).lifetime_args()) if (!r_ok(la_)) bad.push_back(la_.empty() ? std::string("'_") : la_);
            if (decl_lt_arity_(t) > TypeRef(t).lifetime_args().size())
                bad.push_back("'_ (elided lifetime argument of " + type_str(t) + ")");
            for (auto a : TypeRef(t).type_args()) walk(a, "", d + 1);
            return;
        case K::Tuple: for (auto e : TypeRef(t).tuple_elems()) walk(e, "", d + 1); return;
        case K::Array: walk(TypeRef(t).elem(), "", d + 1); return;
        case K::TypeVar: {
            std::string tvn(TypeRef(t).type_var_name());
            if (!tv_ok(tvn)) bad.push_back(tvn + " (no bound reaching " + R + ")");
            return;
        }
        case K::AssocType: {
            // RFC 1214: `T: 'r` discharges `T::Item: 'r`. A declared projection
            // bound (`where T::Item: 'r`) is not parseable today.
            TypeRef base = TypeRef(t).assoc_base();
            if (base && TypeRef(base).kind() == K::TypeVar) {
                if (!tv_ok(TypeRef(base).type_var_name()))
                    bad.push_back(type_str(t) + " (no bound on " + std::string(TypeRef(base).type_var_name()) + " reaching " + R + ")");
            }
            return;
        }
        case K::Closure: {
            if (cid.empty()) return;  // a closure type with no literal in view (a `dyn Fn` value): nothing to read
            auto ce = closure_caps_by_id_.find(cid);
            if (ce == closure_caps_by_id_.end()) return;
            for (auto& [ct, ccid] : ce->second) walk(ct, ccid, d + 1);
            return;
        }
        default: return;
        }
    };
    walk(sp, src_closure_id, 0);
    if (!bad.empty())
        error(std::format("coercion to `dyn {}` requires `{}: {}` — lifetime `{}` may not live long enough (object lifetime bound)",
                          std::string(TypeRef(pdyn).trait_name()), type_str(sp), R, bad.front()));
}

bool SemaChecker::try_implicit_reborrow_mut(lir::LExprPtr& arg, TypeRef pt,
                                              bool allow_downgrade) {
    // Rust auto-reborrows `&mut T` at call/method coercion sites where the
    // formal expects either `&mut T` (mut reborrow) or `&T` (downgrade
    // reborrow as shared). The wrapped expression — AddrOfTemp(Deref(r)) —
    // routes through borrow_check's AddrOfTemp(Deref(VarRef ref-typed))
    // handler, which registers a borrow on r rather than consuming it.
    // Without this, every fn call passing a `&mut T` arg would move it,
    // making Rust-idiom (`f.write_str(x); v.fmt(f);`) reject.
    //
    // The reborrow is purely STRUCTURAL — the wrapped expression has the
    // SAME type as `arg`, so we don't require `types_compatible(arg, pt)`:
    // the existing argument type-check will run after this and flag any
    // genuine mismatch. We only need to know the FORMAL is ref-shaped
    // (mut or shared) so the reborrow makes semantic sense; in particular
    // a generic `pt = &mut Self` (TypeVar pointee) is fine — the reborrow
    // doesn't reify Self.
    if (!arg || !pt) return false;
    if (TypeRef(expr_type(arg)).kind() != LogosType::Kind::MutRef) return false;
    auto pkind = TypeRef(pt).kind();
    bool dest_mut;
    if (pkind == LogosType::Kind::MutRef) dest_mut = true;
    else if (pkind == LogosType::Kind::Ref) {
        // Downgrading reborrow `&mut → &` is correct for fn-arg coercion
        // (Rust auto-downgrades), but at the method-receiver position the
        // formal `&Self` carries Self = `&mut X` for an `impl Trait for &mut
        // X` (Self IS the ref), and downgrading would dispatch through the
        // wrong impl key. Caller passes allow_downgrade=false there.
        if (!allow_downgrade) return false;
        dest_mut = false;
    }
    else if (pkind == LogosType::Kind::Ptr) dest_mut = TypeRef(pt).mut_ptr();
    else return false;
    TypeRef arg_pointee = TypeRef(expr_type(arg)).pointee();
    if (!arg_pointee) return false;
    // Reborrow only applies to a PLACE holding a `&mut T` — a binding (VarRef)
    // or a field access yielding `&mut T`. Bare `&mut x` / `&mut p.f` is
    // already a FRESH borrow expression (AddrOf/AddrOfTemp) — wrapping it in
    // a reborrow shape would hide it from borrow_check's normal recording
    // path, silently dropping the borrow.
    // ⚠ TupleIndex ADDED, AND THIS SITE IS DELIBERATELY *NOT* DELEGATED TO
    // lir_view::is_place_expr, unlike the three match-scrutinee lists.
    // The shape this produces — AddrOfTemp(Deref(<arg>)) — has exactly one
    // recogniser: lir_view::is_reborrow_shape, which matches ONLY
    // AddrOfTemp(Deref(VarRef)), and borrow_check's reborrow handler is keyed
    // on the same. Widening the PRODUCER past what the RECOGNISER accepts
    // manufactures expressions borrow_check does not see as reborrows — which
    // is the failure this function's own comment above already names, and it is
    // PERMISSIVE, so a green corpus cannot see it either.
    // FieldRead is already here and already exercises the non-VarRef path, so
    // TupleIndex rides a proven route: `t.0` is a field whose name is its
    // index. Deref and SliceIndex wait for is_reborrow_shape to be widened
    // first — that is its own arc, not a line in this one.
    // ⚠ Deref ADDED, AND THE BLOCK THAT KEPT IT OUT IS NOW LIFTED. It was held
    // back because the only recogniser of the shape this produces matched
    // `AddrOfTemp(Deref(VarRef))` alone, so `f(*p)` would have been wrapped into
    // `AddrOfTemp(Deref(Deref(p)))` and recognised by nothing. borrow_check's
    // AddrOfTemp arm now PEELS A DEREF CHAIN instead of matching one deref, so
    // the wrap is recorded. MEASURED: `f(*p, *p)` with `p: &mut &mut i64`
    // compiled rc 0 — two live `&mut i64` onto one storage, E0499 — while its
    // one-property twin `f(p, p)` refused.
    // ⚠ THE REMAINING CONSUMER IS CODEGEN: mlir_gen_dyn's dyn_storage_ptr still
    // unwraps exactly one deref, and a wrap it fails to recognise is read as a
    // by-value fat pair — a SEGFAULT, not a refusal. That shape needs a
    // `& &dyn Tr` receiver, which the type grammar does not parse today
    // (measured: "syntax error near '&'"), so it is unreachable rather than
    // handled. If reference-to-reference types ever parse, dyn_storage_ptr must
    // peel the chain before this line is safe.
    // SliceIndex still waits: it has no demonstrated live hole yet, and one
    // spelling at a time is how each of these is measured.
    auto k = expr_ref_of(arg).kind();
    if (k != lir_schema::expr::Code::VarRef &&
        k != lir_schema::expr::Code::FieldRead &&
        k != lir_schema::expr::Code::TupleIndex &&
        k != lir_schema::expr::Code::Deref &&
        k != lir_schema::expr::Code::IndexRead)
        return false;
    // The reborrow carries the reborrowed reference's region (2026-09-02s).
    std::string arg_region(TypeRef(expr_type(arg)).lifetime());
    auto deref = builder().deref(std::move(arg), arg_pointee);
    arg = builder().addr_of_temp(std::move(deref), /*is_mut=*/dest_mut,
                                  make_ref(dest_mut, arg_pointee, arg_region));
    return true;
}

bool SemaChecker::coerce_dyn_upcast(lir::LExprPtr& arg, TypeRef pt) {
    if (!arg || !pt) return false;
    TypeRef at(expr_type(arg));
    if (TypeRef(at).kind() == LogosType::Kind::Error) return false;
    // Source must be a trait object (bare or behind a ref).
    TypeRef src_to = at;
    if ((at.kind() == LogosType::Kind::Ref || at.kind() == LogosType::Kind::MutRef) &&
        at.pointee()) src_to = at.pointee();
    if (TypeRef(src_to).kind() != LogosType::Kind::TraitObject) return false;
    if (types_compatible(at, pt)) return false;  // identical dyn — no upcast
    // Peel param to its bare TraitObject.
    TypeRef pdyn = pt;
    if ((TypeRef(pt).kind() == LogosType::Kind::Ref ||
         TypeRef(pt).kind() == LogosType::Kind::MutRef) && TypeRef(pt).pointee())
        pdyn = TypeRef(pt).pointee();
    if (TypeRef(pdyn).kind() != LogosType::Kind::TraitObject) return false;
    std::string sub(TypeRef(src_to).trait_name());
    std::string super(TypeRef(pdyn).trait_name());
    if (sub.empty() || super.empty()) return false;
    // Super must be a (transitive) supertrait of Sub. Works for bare-`dyn`
    // sources too (ref_arg_satisfies_dyn requires a ref source, so reachability
    // is checked here directly).
    logos::compiler::StrSet seen;
    std::function<bool(const std::string&)> reaches =
        [&](const std::string& tn) -> bool {
            if (!seen.insert(tn).second) return false;
            if (tn == super) return true;
            auto it = traits_.find(tn);
            if (it == traits_.end()) return false;
            for (auto& s : it->second.supertraits)
                if (reaches(s.trait_name)) return true;
            return false;
        };
    if (sub == super || !reaches(sub)) return false;
    mark_coercion_source_moved(arg);   // see mark_coercion_source_moved (R2)
    arg = builder().cast(std::move(arg), pt);
    return true;
}

bool SemaChecker::coerce_arg_to_dyn(lir::LExprPtr& arg, TypeRef pt) {
    if (!arg || !pt) return false;
    if (TypeRef(expr_type(arg)).kind() == LogosType::Kind::Error) return false;
    if (types_compatible(expr_type(arg), pt)) return false;  // already fits
    // Implicit CoerceUnsized for a smart-pointer/wrapper struct param
    // (`Rc<A>` → `Rc<dyn Tr>`): rebuild unsizing the inner field. Mirrors the
    // explicit `as` path; closes GAP-C for the flipped struct repr.
    if (try_struct_unsize_coerce(arg, pt)) return true;
    // Peel a `&dyn` / `&mut dyn` param to the bare TraitObject.
    TypeRef pdyn = pt;
    auto pk = TypeRef(pt).kind();
    if ((pk == LogosType::Kind::Ref || pk == LogosType::Kind::MutRef) &&
        TypeRef(pt).pointee())
        pdyn = TypeRef(pt).pointee();
    if (TypeRef(pdyn).kind() != LogosType::Kind::TraitObject) return false;
    if (!ref_arg_satisfies_dyn(expr_type(arg), pdyn)) return false;
    mark_coercion_source_moved(arg);   // see mark_coercion_source_moved (R3)
    arg = builder().cast(std::move(arg), pt);
    return true;
}

// ── #68 CLASS: an aggregate literal's SLOT TYPES have no second source ─────
// A struct FIELD's type comes from the struct declaration, so mlir-gen can
// unsize `&Concrete` into a `&dyn Trait` field in every context and does
// (gen_struct_lit — verified still green here in every context probed). A TUPLE
// is structural (its type IS its elements' types) and an ARRAY literal's element
// type is read off the LITERAL NODE too (gen_arr_lit's `logos_elem`,
// tuple_llvm_type) — so `(&a, 7)` is TYPED `(&Sq, i64)`, a 16-byte aggregate,
// and every consumer that expects `(&dyn Shape, i64)` reads 24.
// The only site that used to repair it was the `let`-annotation `retype_expr`
// in lower_let, which is why the defect was invisible under an annotated `let`
// and fatal everywhere else (MEASURED, one runtime value each: call arg 139,
// nested tuple 2, fn return 1, match arm 1, generic arg 1, struct field 1,
// assignment 1, array-of-tuples 1; `--emit-mlir` showed the caller building
// `struct<(ptr, i64)>` for a callee reading `struct<(struct<(ptr, ptr)>, i64)>`).
//
// So the repair is the SAME operation lower_let already performed, moved to the
// one judgment that every position goes through (`expect_type` →
// `coerce_arg_to_param`): stamp the expected tuple type onto the literal, IN
// PLACE. mlir-gen's ETupleLit arm then does the unsize from the slot type, the
// way the struct and array arms always did. In place matters: a match ARM or an
// array ELEMENT is a sub-expression of a node this function is handed, and a
// rebuild would have to re-emit the arm/pattern mirrors — the walk below
// retypes the literal where it sits instead.

// Does slot type `tgt` accept element type `at` only by an UNSIZE to `&dyn`?
// `&dyn Trait` IS Kind::TraitObject (`Ref<UnsizedDyn<Trait>>` is canonicalised
// to it at resolve time), so the slot is asked for DIRECTLY, never peeled.
// ⚠ THE `*const dyn` CLAIM THIS COMMENT USED TO CARRY WAS WRONG TWICE OVER, and
// it is corrected here rather than repeated. It said the single line above
// "keeps `*const dyn Trait` out" because a raw dyn pointer "deliberately keeps
// 8-byte handle semantics". Both halves are refuted by measurement:
//   • the WIDTH: `sizeof::<*const dyn Shape>() == 16`
//     (/home/logos/sandbox/aggunsize/z_rawdyn.logos), and coerce_to_dyn in
//     mlir_gen_dyn.cpp states the model outright — "`&dyn`/`*dyn`/`Box<dyn>` are
//     all uniform 16-byte fat", the thin-handle path having been REMOVED as
//     provably unreachable across the whole corpus.
//   • the EXCLUSION: there is none. A `*const dyn Trait` SLOT is canonicalised
//     to Kind::TraitObject exactly like `&dyn Trait`, so it is asked and
//     answered here — MEASURED, `fn take(t: (*const dyn Shape, i64))` fed
//     `let p: *const Sq = &a; let t = (p, 7i64); take(t)` is refused with
//     "aggregate slot `.0`: expected &dyn Shape, got *const Sq", and the same
//     slot's `sizeof::<(*const dyn Shape, i64)>()` is 24, not 16
//     (z_rawdyn_tuple.logos / z_rawdyn_cast.logos).
// MEASURED: the peel-Ref-first spelling — the shape
// mlir-gen's arm uses — matched NOTHING here (`slot target=&dyn Shape … tk=28`).
bool SemaChecker::aggregate_slot_needs_unsize(TypeRef at, TypeRef tgt) {
    if (!at || !tgt) return false;
    if (TypeRef(tgt).kind() != LogosType::Kind::TraitObject) return false;
    if (at.kind() == LogosType::Kind::Error) return false;
    // ── #95/M2: AN OWNING `Box<dyn Trait>` SLOT ───────────────────────────────
    // The first cut of #95 EXCLUDED the owning form from both halves and
    // disclosed it as unmeasured. Measured, it was a crash, not a narrowing:
    // `(Box<Sq>, i64)` against `(Box<dyn Shape>, i64)` compiled and SIGSEGVed in
    // the tuple, the array and the struct-field shapes
    // (/home/logos/sandbox/vfy95/h/h0{1,2,8}*.logos, rc 139 each).
    //
    // AND THE DECISION IS SETTLED BY MEASUREMENT, not by symmetry-of-argument:
    // an ANNOTATED tuple literal — `let t: (Box<dyn Shape>, i64) =
    // (Box::new(Sq{..}), 7i64)` — ALREADY LOWERS CORRECTLY (rc 42, probe p/b1),
    // because mlir-gen's tuple arm reads the slot type off the literal node and
    // unsizes the owning pointer there exactly as it does the borrowed one. So
    // the fat half is not missing at codegen; it is missing at SEMA, which never
    // stamped the literal because this predicate demanded a `&`/`&mut` source.
    // The owning source is the Box STRUCT (`Box<Sq>` is Kind::Struct; only
    // `Box<dyn T>` is a Kind::TraitObject with an owning kind), so it is asked
    // for here in its own arm, and the impl/auto-bound question is delegated —
    // unchanged — by asking it about a BORROW of the payload: what may be erased
    // is a property of the payload type, and only the release semantics differ
    // between `&dyn` and `Box<dyn>`. The HOISTED owning form has no literal to
    // stamp and is refused by find_uncoerced_aggregate_slot, same as borrowed.
    if (TypeRef(tgt).owning_trait_object()) {
        if (!is_stdlib_box(at)) return false;   // already fat, or not a Box
        auto ta = at.type_args();
        if (ta.size() != 1 || !ta[0]) return false;
        if (TypeRef(ta[0]).kind() == LogosType::Kind::TraitObject) return false;
        return ref_arg_satisfies_dyn(make_ref(false, ta[0]), tgt);
    }
    // Already the fat pair (a `&a as &dyn Shape` element, or a `&dyn` binding):
    // NOT an unsize, and must not be counted as one — no double coercion.
    if (at.kind() != LogosType::Kind::Ref && at.kind() != LogosType::Kind::MutRef)
        return false;
    return ref_arg_satisfies_dyn(at, tgt);
}

bool SemaChecker::retype_aggregate_lit_to(lir_view::ExprRef er, TypeRef target) {
    if (!er || !target) return false;
    TypeRef et(er.type(cur_prog_->type_pool.impl()));
    if (!et) return false;
    // ── the WRAPPERS: the literal is a sub-expression, retype it where it is ──
    switch (er.kind()) {
    case lir_schema::expr::Code::MatchExpr: {
        bool any = false;
        lir_view::EMatchExprView{er}.each_arm([&](lir_view::EMatchArmRef a) {
            if (auto v = a.value()) any = retype_aggregate_lit_to(v, target) || any;
        });
        if (any) builder().retype_expr(er, target);
        return any;
    }
    case lir_schema::expr::Code::IfExpr: {
        lir_view::EIfExprView v{er};
        bool any = false;
        if (v.then_val()) any = retype_aggregate_lit_to(v.then_val(), target) || any;
        if (v.else_val()) any = retype_aggregate_lit_to(v.else_val(), target) || any;
        if (any) builder().retype_expr(er, target);
        return any;
    }
    case lir_schema::expr::Code::BlockExpr: {
        lir_view::EBlockExprView v{er};
        bool any = v.result() && retype_aggregate_lit_to(v.result(), target);
        if (any) builder().retype_expr(er, target);
        return any;
    }
    case lir_schema::expr::Code::EnumLit:
    case lir_schema::expr::Code::EnumLitData:
        // #95/M3 — AN ENUM LITERAL IS A COERCION SITE TOO. `take(Some(&a))`
        // against `fn take(o: Option<&dyn Shape>)` used to COMPILE AND RETURN
        // THE WRONG ANSWER (rc 1, /home/logos/sandbox/vfy95/h2/g01_option_dyn):
        // the literal was typed `Option<&Sq>` from its payload and nothing ever
        // re-asked. mlir-gen's EnumLitData arm already unsizes a payload whose
        // DECLARED slot type is a TraitObject (it reads the variant's payload
        // types out of the instance named by the node's TYPE, then calls
        // coerce_value_to_dyn_if_needed) — which is why the annotated spelling
        // `let o: Option<&dyn Shape> = Some(&a)` was correct all along. So the
        // repair is the same one the tuple/array arms get: stamp the expected
        // instance onto the literal node, and let the slot rule below decide
        // whether that is legitimate.
    case lir_schema::expr::Code::TupleLit:
    case lir_schema::expr::Code::ArrLit:
        // A WRAPPER is never short-circuited on `et == target`: a match whose
        // arms are aggregate literals gets its own type from arm 0 and can
        // already READ as the dyn type while every arm still carries the thin
        // one (MEASURED: ar_match, `[&dyn Shape; 2]` outside, `[&Sq; 2]` in the
        // arms, SIGSEGV). Only a LITERAL that already IS the target is done.
        if (et == target) return false;
        break;
    default: return false;
    }
    // ── the LITERAL ───────────────────────────────────────────────────────────
    // One slot: unsize / nested stamp / already fits / REFUSE. Shared by both
    // literal shapes so the tuple and array arms cannot drift apart again.
    auto slot = [&](lir_view::ExprRef el, TypeRef ce, TypeRef te,
                    bool& has_unsize, uint64_t idx) -> bool {
        if (!ce || !te) return false;
        if (aggregate_slot_needs_unsize(ce, te)) { has_unsize = true; return true; }
        if (el && retype_aggregate_lit_to(el, te)) { has_unsize = true; return true; }
        // The PERMISSIVE twin, found by this round's own probe. A `&dyn Trait`
        // slot fed a `&Concrete` that does NOT implement the trait is accepted
        // by types_compatible (its Struct → TraitObject branch is a blanket
        // accept, "impl check deferred to codegen") — and in the AGGREGATE case
        // codegen never gets to run its check, because without a stamp nothing
        // ever attempts the coercion: `(&a, 7i64)` against `(&dyn Other, i64)`
        // with `Sq: !Other` wrote an object file, while the plain arg spelling
        // `take(&a)` is refused with "no vtable for 'Sq' as '&dyn Other'".
        // Refuse HERE, and only for a CONCRETE pointee — a TypeVar pointee is
        // mono's judgment (ref_arg_satisfies_dyn answers it from the bound set,
        // which is incomplete before substitution) and is left alone.
        // #95/M2 — and the OWNING spelling of the same hole. `Box<Sq>` fed to a
        // `Box<dyn Other>` slot with `Sq: !Other` reaches here with a Struct
        // source, not a Ref one; without this arm it fell straight through to
        // types_compatible's blanket accept and wrote an object file whose
        // vtable half is uninitialised. The erased-type question is asked about
        // the payload, exactly as aggregate_slot_needs_unsize asks it.
        TypeRef owning_payload{nullptr};
        if (TypeRef(te).kind() == LogosType::Kind::TraitObject &&
            TypeRef(te).owning_trait_object() && is_stdlib_box(ce)) {
            auto ta = ce.type_args();
            if (ta.size() == 1 && ta[0]) {
                auto pk = TypeRef(ta[0]).kind();
                if (pk != LogosType::Kind::TypeVar &&
                    pk != LogosType::Kind::AssocType &&
                    pk != LogosType::Kind::TraitObject &&
                    pk != LogosType::Kind::Error)
                    owning_payload = ta[0];
            }
        }
        if (owning_payload) {
            auto [es, gs] = type_str_pair(te, ce);
            // ⚠ NOT SPELLED AS THE MISMATCH VERDICT, and that is the point.
            // `expected {}, got {}` is expect_type's monopoly
            // (scripts/lint-mismatch-monopoly.sh), and this is a DIFFERENT
            // verdict: the types are not merely unequal, the element cannot be
            // unsized here because the trait is not implemented. Re-spelling it
            // as a mismatch would have made the lint count three emitters of
            // one verdict — which is exactly the sieve of per-site special
            // cases that lint exists to stop.
            error(std::format("aggregate element {}: slot type {} needs an "
                              "unsize from {}, but the type does not implement "
                              "the trait, so the element's vtable half would be "
                              "uninitialised",
                              idx, es, gs));
            return false;
        }
        if (TypeRef(te).kind() == LogosType::Kind::TraitObject &&
            (ce.kind() == LogosType::Kind::Ref ||
             ce.kind() == LogosType::Kind::MutRef) &&
            ce.pointee() &&
            TypeRef(ce.pointee()).kind() != LogosType::Kind::TypeVar &&
            TypeRef(ce.pointee()).kind() != LogosType::Kind::AssocType &&
            TypeRef(ce.pointee()).kind() != LogosType::Kind::TraitObject &&
            TypeRef(ce.pointee()).kind() != LogosType::Kind::Error) {
            auto [es, gs] = type_str_pair(te, ce);
            // ⚠ NOT SPELLED AS THE MISMATCH VERDICT, and that is the point.
            // `expected {}, got {}` is expect_type's monopoly
            // (scripts/lint-mismatch-monopoly.sh), and this is a DIFFERENT
            // verdict: the types are not merely unequal, the element cannot be
            // unsized here because the trait is not implemented. Re-spelling it
            // as a mismatch would have made the lint count three emitters of
            // one verdict — which is exactly the sieve of per-site special
            // cases that lint exists to stop.
            error(std::format("aggregate element {}: slot type {} needs an "
                              "unsize from {}, but the type does not implement "
                              "the trait, so the element's vtable half would be "
                              "uninitialised",
                              idx, es, gs));
            return false;
        }
        return types_compatible(ce, te);
    };
    // NARROW ON PURPOSE. The stamp only happens when at least one slot is a
    // VALIDATED `&Concrete` → `&dyn Trait` unsize (directly, or inside a nested
    // literal), and every other slot already fits. Anything else leaves the
    // literal completely alone and the ordinary mismatch diagnostic downstream
    // still fires.
    bool has_unsize = false;
    if (er.kind() == lir_schema::expr::Code::EnumLit ||
        er.kind() == lir_schema::expr::Code::EnumLitData) {
        // The enum instance's payload slot types come from the VARIANT
        // declaration substituted with the TARGET's type-args — the same
        // projection retype_enum_lit_recursive does, and the same one mlir-gen
        // will do from the stamped node. A payload-less variant (`None`) has no
        // slot to unsize, so it never stamps here: `None` against
        // `Option<&dyn Shape>` is a bare instance question, answered upstream.
        if (TypeRef(target).kind() != LogosType::Kind::Enum) return false;
        if (et.kind() != LogosType::Kind::Enum) return false;
        if (et.enum_name() != TypeRef(target).enum_name()) return false;
        if (et.pkg_name() != TypeRef(target).pkg_name()) return false;
        if (er.kind() != lir_schema::expr::Code::EnumLitData) return false;
        lir_view::EEnumLitDataView v{er};
        auto [pkg, esi] = find_enum_by_name(std::string(v.enum_name()));
        (void)pkg;
        if (!esi) return false;
        const SemaVariantInfo* vinfo = nullptr;
        std::string vn(v.variant());
        for (auto& vv : esi->variants) if (vv.name == vn) { vinfo = &vv; break; }
        if (!vinfo || vinfo->payload_types.empty()) return false;
        SemaSubst subst;
        auto cta = TypeRef(target).type_args();
        if (cta.size() != esi->type_params.size()) return false;
        for (size_t i = 0; i < esi->type_params.size(); ++i)
            if (cta[i]) subst[esi->type_params[i].name] = cta[i];
        std::vector<lir_view::ExprRef> pl;
        v.each_payload([&](lir_view::ExprRef pe){ pl.push_back(pe); });
        if (pl.size() != vinfo->payload_types.size()) return false;
        for (size_t i = 0; i < pl.size(); ++i) {
            TypeRef tt = vinfo->payload_types[i];
            if (!tt) return false;
            if (!subst.empty()) tt = subst_type_sema(tt, subst);
            if (!pl[i]) return false;
            if (!slot(pl[i], TypeRef(pl[i].type(cur_prog_->type_pool.impl())),
                      TypeRef(tt), has_unsize, i))
                return false;
        }
        if (!has_unsize) return false;
        builder().retype_expr(er, target);
        // Nested enum payloads (`Some(Some(&a))`) need the SAME projection one
        // level down, and that walk already exists.
        retype_enum_lit_recursive(er, target);
        return true;
    }
    if (er.kind() == lir_schema::expr::Code::TupleLit) {
        if (TypeRef(target).kind() != LogosType::Kind::Tuple) return false;
        if (et.kind() != LogosType::Kind::Tuple) return false;
        const auto& tgt_elems = TypeRef(target).tuple_elems();
        const auto& cur_elems = et.tuple_elems();
        if (tgt_elems.size() != cur_elems.size()) return false;
        lir_view::ETupleLitView v{er};
        if (v.count() != tgt_elems.size()) return false;
        for (uint64_t i = 0; i < v.count(); ++i)
            if (!slot(v.elem(i), TypeRef(cur_elems[i]), TypeRef(tgt_elems[i]),
                      has_unsize, i))
                return false;
    } else {
        if (TypeRef(target).kind() != LogosType::Kind::Array) return false;
        if (et.kind() != LogosType::Kind::Array) return false;
        TypeRef te = TypeRef(target).elem();
        if (!te) return false;
        lir_view::EArrLitView v{er};
        // A stamp must never change the LENGTH — `[x; 2]` is not an `[T; 3]`.
        if (TypeRef(target).arr_size() != v.count()) return false;
        for (uint64_t i = 0; i < v.count(); ++i) {
            auto el = v.elem(i);
            if (!el) return false;
            if (!slot(el, TypeRef(el.type(cur_prog_->type_pool.impl())), te,
                      has_unsize, i))
                return false;
        }
    }
    if (!has_unsize) return false;
    builder().retype_expr(er, target);
    return true;
}

lir::LExprPtr SemaChecker::lower_typaram_static_method(
        const std::string& cname, const std::string& mname,
        std::vector<TypeRef> explicit_targs,
        std::vector<lir::LExprPtr> arg_exprs) {
    auto bit = current_type_bounds_.find(cname);
    if (bit == current_type_bounds_.end()) return nullptr;
    // Walk the type-param's bounds (+ supertraits) for a STATIC method `mname`
    // (first param isn't `Self`).
    const SemaTraitMethodInfo* m = nullptr;
    bool prov_trait_has_targs = false;
    logos::compiler::StrSet seen;
    std::function<void(const std::string&)> walk = [&](const std::string& tn) {
        if (m || !seen.insert(tn).second) return;
        auto it = find_trait_iter_scoped(tn);
        if (it == traits_.end()) return;
        for (auto& mm : it->second.methods) {
            if (mm.name != mname) continue;
            bool is_static = mm.param_types.empty() ||
                !(mm.param_types[0] &&
                  TypeRef(mm.param_types[0]).kind() == LogosType::Kind::TypeVar &&
                  TypeRef(mm.param_types[0]).type_var_name() == "Self");
            if (is_static) {
                m = &mm;
                prov_trait_has_targs = !it->second.type_params.empty();
                return;
            }
        }
        for (auto& s : it->second.supertraits) walk(s.trait_name);
    };
    for (auto& b : bit->second) { walk(b.trait_name); if (m) break; }
    if (!m) return nullptr;
    // Multi-param-trait static dispatch (`S: Collect<A>` / `Sum<Item>`, trait WITH
    // type-args) is disambiguated by mono via the arg-type suffix and needs the
    // type-args left EMPTY at sema — passing the method's own type-args here
    // breaks that retarget (Gap A'). Defer those to the caller's old path; the
    // general resolver handles only single-dispatch traits (Self-keyed, no trait
    // type-args — e.g. `Zone`).
    if (prov_trait_has_targs) return nullptr;
    // GENERALIZATION: synthesize a SemaFuncInfo from the trait method (Self → the
    // type-param Z), then route through the SAME resolver every other generic call
    // uses — finish_generic_call. It does turbofish + arg-inference + return-hint
    // inference + fn-family-bound propagation uniformly, then emits a call to the
    // abstract `Z__method` symbol (passthrough, no re-mangling) that mono retargets
    // to `Concrete__method::<..>`. Replaces the hand-rolled partial handling.
    SemaSubst self_subst;
    self_subst["Self"] = current_type_params_.count(cname)
        ? current_type_params_[cname] : make_typevar(cname);
    SemaFuncInfo synth;
    synth.type_params = m->type_params;
    synth.param_types.reserve(m->param_types.size());
    for (auto& pt : m->param_types)
        synth.param_types.push_back(pt ? subst_type_sema(pt, self_subst) : pt);
    synth.ret_type = m->ret_type ? subst_type_sema(m->ret_type, self_subst) : void_t();
    synth.is_unsafe = m->is_unsafe;
    synth.impl_target_pattern = nullptr;
    synth.body_always_diverges = false;
    // The composed base below is `<cname>__<mname>`; carry both parts so no
    // consumer has to recover them by cutting at a `__`.
    synth.owner_struct = cname;
    synth.is_method    = true;
    return finish_generic_call(cname + "__" + mname, synth,
                               std::move(explicit_targs), std::move(arg_exprs));
}

lir::LExprPtr SemaChecker::lower_static_call(TinyMapView node) {
    // Phase 1B-5 parity for TYPE-side turbofish (`PkdArray::<str>::format`):
    // bare `[T]`/`str`-as-unsized/`dyn` type args are legal when the class's
    // param is `?Sized` (or a partial spec may govern). resolve_generic's
    // Sized-enforcement still rejects genuinely wrong args; without this the
    // arg canonicalised to the VALUE form (&[u8]) and the instantiation
    // family silently changed.
    struct UOkGuard {
        bool& flag; bool saved;
        UOkGuard(bool& f, bool v) : flag(f), saved(f) { flag = v; }
        ~UOkGuard() { flag = saved; }
    };
    std::optional<UOkGuard> static_call_uok_;
    {
        std::string cn0(str_of(node.get(la::RECEIVER.code)));
        auto [p0, ssi0] = find_struct_by_name(cn0);
        (void)p0;
        bool relax = false;
        if (ssi0) {
            for (auto& tp : ssi0->type_params)
                if (!tp.implicit_sized) { relax = true; break; }
            if (!relax && struct_has_specs(cn0)) relax = true;
        }
        if (relax) static_call_uok_.emplace(unsized_ok_, true);
    }

    std::string class_name(str_of(node.get(la::RECEIVER.code)));
    auto method_name = str_of(node.get(la::NAME.code));

    // T2-28 (Increment 2): a qualified `pkg.path.Type::member(args)` is parsed
    // as the qualified-CALL shape (RECEIVER = first segment, QUAL_PARTS = the
    // rest, member in CALLEE) and delegated here by lower_call once it finds no
    // free fn of that name in the package. The LAST dotted segment is the type;
    // the package prefix is dropped (type resolution searches by name). Clear
    // the package qualifier — type/method resolution + arg lowering are not
    // package-filtered (only free-fn lookups are).
    if (node.has_key(la::QUAL_PARTS)) {
        auto parts = arr_of(node.get(la::QUAL_PARTS.code));
        if (parts.size() >= 1)
            class_name = std::string(
                str_of(map_of(parts.get(parts.size() - 1)).get(la::NAME.code)));
        if (method_name.empty()) method_name = str_of(node.get(la::CALLEE.code));
        call_pkg_qualifier_.clear();
    }

    // G153-4: `Self::method()` inside an impl body — resolve `Self` to the
    // impl's concrete type name (bound in current_type_params_["Self"]) so the
    // static method resolves, exactly as if the type name were written.
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
                }
                // PROBE selflitv / selfv / selfvg (generic literal) — src/compiler/PROBES.md 2026-09-13a.
                if ((logos::probe::on("selflitv") || logos::probe::on("selfv") || logos::probe::on("selfvg") || logos::probe::on("selfvee") || logos::probe::on("selfveu")) && str_of(node.get(la::NAME.code)) == "Self") {
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
                    lit_type = slit_is_zoned
                        ? make_generic_datatype(std::string(sname), args, nl)
                        : make_generic_struct(std::string(sname), args, nl);
                }
            }
        }
        // B77: verify generic-struct's `where 'a: 'b` constraints.
        check_struct_lit_outlives(std::string(sname),
                                  sinfo.lifetime_params,
                                  sinfo.lifetime_outlives,
                                  TypeRef(lit_type).lifetime_args(),
                                  sinfo.fields,
                                  fields);

        return builder().struct_lit(concrete, std::move(fields), lit_type);
    }

    // Non-generic struct: validate against template fields directly.
    StrMap<bool> initialized;
    for (auto& f : sinfo.fields) initialized[std::string(f.name)] = false;
    for (auto& [fname, fval] : fields) {
        auto it = initialized.find(fname);
        if (it == initialized.end()) {
            // Check variadic
            bool matched_variadic = false;
            for (auto& f : sinfo.fields) {
                if (f.is_variadic && fname.starts_with(f.name) && fname.size() > f.name.size() + 1 && fname[f.name.size()] == '_') {
                    initialized[std::string(f.name)] = true;
                    matched_variadic = true;
                    auto ft = f.type;
                    if (ft)
                        expect_type(fval, ft, CoercePos::StructLitField,
                                    std::format("struct literal '{}' field '{}':",
                                                sname, fname));
                    if (ft && TypeRef(ft).kind() != LogosType::Kind::Error &&
                        TypeRef(expr_type(fval)).kind() == LogosType::Kind::IntLit)
                        if (auto v = get_intlit_value(fval))
                            if (!intlit_fits(*v, TypeRef(ft).kind()))
                                error(std::format("struct literal '{}' field '{}': value {} does not fit in {}",
                                      sname, fname, *v, type_str(ft)));
                    break;
                }
            }
            if (!matched_variadic)
                error(std::format("struct literal '{}': unknown field '{}'", sname, fname));
        } else {
            if (it->second) {
                error(std::format("struct literal '{}': duplicate field '{}'", sname, fname));
                continue;
            }
            it->second = true;
            auto ft = field_type_of(std::string(sname), fname);
            // Same as the generic path above: a field initializer is a write
            // to a typed place and gets a place write's coercions.
            if (ft)
                expect_type(fval, ft, CoercePos::StructLitField,
                            std::format("struct literal '{}' field '{}':",
                                        sname, fname));
            // B68.2: variance check at struct-lit field-init coercion.
            // Permissive — struct's lifetime args are inferred at this site.
            // PROBE ltmintinst: and "inferred at this site" is exactly why the
            // struct's OWN binders are not regions to compare against here —
            // see the generic path above for the measurement
            // (regions-mock-codegen). THIS is the site a struct with lifetime
            // params but no TYPE params takes.
            TypeRef ft_cmp2 = ft;
            if (ft && (logos::probe::arm_inst() || logos::probe::arm_subst())) {
                auto [_slp_pkg, _slp] = find_struct_by_name(std::string(sname));
                (void)_slp_pkg;
                if (_slp && !_slp->lifetime_params.empty()) {
                    auto blift2 = structlit_lt_subst_(_slp->lifetime_params,
                                                      _slp->fields, fields,
                                                      _slp->package.empty()
                                                        ? std::string(sname)
                                                        : _slp->package + "." + std::string(sname));
                    if (!blift2.empty()) {
                        ft_cmp2 = subst_type_sema(ft, {}, blift2);
                        if (ft_cmp2 != ft) logos::probe::census("structlit.field.instantiated");
                    }
                }
            }
            if (ft)
                check_variance(expr_type(fval), ft_cmp2,
                               std::format("struct literal '{}' field '{}'", sname, fname),
                               /*permissive=*/true);
            // T1-12 (audit-v2): dyn+auto bound at field-init coercion
            // (`Holder { d: &not_send }` against `d: &dyn Trait + Send`).
            if (ft)
                check_dyn_auto_bounds_at_coercion(fval, ft);
            // Check IntLit field value fits in the declared field type.
            if (ft && TypeRef(expr_type(fval)).kind() == LogosType::Kind::IntLit)
                if (auto v = get_intlit_value(fval))
                    if (!intlit_fits(*v, TypeRef(ft).kind()))
                        error(std::format("struct literal '{}' field '{}': value {} does not fit in {}",
                              sname, fname, *v, type_str(ft)));
            // Check array literal elements against narrow array field type.
            if (ft && TypeRef(ft).kind() == LogosType::Kind::Array && TypeRef(ft).elem() &&
                TypeRef(expr_type(fval)).kind() == LogosType::Kind::Array) {
                auto vr = expr_ref_of(fval);
                if (vr.kind() == lir_schema::expr::Code::ArrLit) {
                    lir_view::EArrLitView al{vr};
                    for (uint64_t i = 0; i < al.count(); ++i) {
                        auto el = al.elem(i);
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (!intlit_fits(*v, TypeRef(ft).elem().kind()))
                                    error(std::format("struct literal '{}' field '{}': array element {}: value {} does not fit in {}",
                                          sname, fname, i, *v, type_str(TypeRef(ft).elem())));
                    }
                }
            }
            // Check tuple literal elements against narrow tuple field element types.
            if (ft && TypeRef(ft).kind() == LogosType::Kind::Tuple && TypeRef(expr_type(fval)).kind() == LogosType::Kind::Tuple) {
                auto vr = expr_ref_of(fval);
                if (vr.kind() == lir_schema::expr::Code::TupleLit) {
                    lir_view::ETupleLitView tl{vr};
                    uint64_t i = 0;
                    tl.each_elem([&](lir_view::ExprRef el) {
                        if (i >= TypeRef(ft).tuple_elems().size()) { ++i; return; }
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (TypeRef(ft).tuple_elems()[i] && !intlit_fits(*v, TypeRef(TypeRef(ft).tuple_elems()[i]).kind()))
                                    error(std::format("struct literal '{}' field '{}': tuple element {}: value {} does not fit in {}",
                                          sname, fname, i, *v, type_str(TypeRef(ft).tuple_elems()[i])));
                        if (TypeRef(ft).tuple_elems()[i] && TypeRef(TypeRef(ft).tuple_elems()[i]).kind() == LogosType::Kind::Array &&
                            TypeRef(TypeRef(ft).tuple_elems()[i]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                            el.kind() == lir_schema::expr::Code::ArrLit) {
                            lir_view::EArrLitView ial{el};
                            for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                                auto iel = ial.elem(ii);
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (!intlit_fits(*v, TypeRef(TypeRef(ft).tuple_elems()[i]).elem().kind()))
                                            error(std::format("struct literal '{}' field '{}': tuple element {}: array element {}: value {} does not fit in {}",
                                                  sname, fname, i, ii, *v, type_str(TypeRef(TypeRef(ft).tuple_elems()[i]).elem())));
                            }
                        }
                        if (TypeRef(ft).tuple_elems()[i] && TypeRef(TypeRef(ft).tuple_elems()[i]).kind() == LogosType::Kind::Tuple &&
                            el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                            el.kind() == lir_schema::expr::Code::TupleLit) {
                            lir_view::ETupleLitView itl{el};
                            uint64_t ii = 0;
                            itl.each_elem([&](lir_view::ExprRef iel) {
                                if (ii >= TypeRef(TypeRef(ft).tuple_elems()[i]).tuple_elems().size()) { ++ii; return; }
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (TypeRef(TypeRef(ft).tuple_elems()[i]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(ft).tuple_elems()[i]).tuple_elems()[ii]).kind()))
                                            error(std::format("struct literal '{}' field '{}': tuple element {}: sub-element {}: value {} does not fit in {}",
                                                  sname, fname, i, ii, *v, type_str(TypeRef(TypeRef(ft).tuple_elems()[i]).tuple_elems()[ii])));
                                ++ii;
                            });
                        }
                        ++i;
                    });
                }
            }
        }
    }
    // Handle struct update syntax: Foo { x: 1, ..base }
    // For any field not explicitly set, read it from the base expression.
    if (node.has_key(la::BASE)) {
        auto base_node = map_of(node.get(la::BASE.code));
        auto base_expr = lower_expr(base_node);
        // Sprint 3.4: enforce that `..base` carries the same struct type as
        // the constructor (closes B-li-03 — Foo+..bar silently spread foreign
        // bytes into Bar).
        if (expr_type(base_expr)) {
            TypeRef bt = expr_type(base_expr);
            auto bk = bt.kind();
            bool ok = (bk == LogosType::Kind::Struct || bk == LogosType::Kind::ZonedStruct)
                      && bt.struct_name() == std::string_view(sname);
            if (!ok) {
                error(std::format(
                    "struct literal '{}': '..base' must have type '{}' (got '{}')",
                    sname, sname,
                    (bk == LogosType::Kind::Error) ? "?" : type_str(bt)));
            }
        }
        // Determine base variable name for EVarRef (simple case)
        std::string base_var;
        {
            auto er = expr_ref_of(base_expr);
            if (er.kind() == lir_schema::expr::Code::VarRef)
                base_var = std::string(lir_view::EVarRefView{er}.name());
        }
        for (auto& [fname, inited] : initialized) {
            if (!inited) {
                inited = true;
                auto ft = field_type_of(std::string(sname), fname);
                lir::LExprPtr recv = nullptr;
                if (!base_var.empty()) {
                    recv = builder().var_ref(base_var, expr_type(base_expr));
                } else {
                    // Complex base: re-lower (might evaluate twice, but rare)
                    recv = lower_expr(base_node);
                }
                auto field_val = builder().field_read(std::move(recv), fname, ft ? ft : error_t());
                fields.push_back({fname, std::move(field_val)});
            }
        }
    }

    // §6.1: union literals are partial-by-design — skip the
    // missing-field check (only one field is "active").
    if (!sinfo.is_union) {
        for (auto& [fname, init] : initialized)
            if (!init)
                error(std::format("struct literal '{}': field '{}' not initialized", sname, fname));
    }

    // Move semantics: mark Move-typed field values as consumed.
    for (auto& [fname, fval] : fields) {
        if (fval && is_move_type(expr_type(fval)))
            mark_moved_expr(expr_ref_of(fval));
    }

    std::vector<std::string> ng_lt_args;
    if (hint_struct_type_ && TypeRef(hint_struct_type_).struct_name() == std::string(sname))
        ng_lt_args = TypeRef(hint_struct_type_).lifetime_args();
    // ── SUBSTITUTE AT THE LITERAL (probes ltsubstlit / ltmintsubst) ─────────
    // A struct's own lifetime parameter arrives here UNSUBSTITUTED, and the
    // literal's lifetime args are taken from the HINT — i.e. from the type the
    // context EXPECTS, never from the values actually stored. So
    // `fn mk<'b,'a>(y:&'b i64) -> Holder<'a> { return Holder{v:y}; }` builds
    // `Holder<'a>` out of a `&'b` field and the return comparison is 'a vs 'a.
    // That is why `lifereg_unmentbind` catches this program only when the
    // struct's binder happens to be SPELLED like the fn's (PROBES.md 2026-08-31g,
    // the u7/u8 pair, pinned as fixtures). The fix is substitution: pair each
    // declared field type against the actual field VALUE's type and read the
    // struct's binder off the value's region.
    // LANDED 2026-09-09 (was PROBE ltsubstlit — its only site, retired by this
    // landing). The literal's lifetime args are read off its VALUES.
    if (!sinfo.lifetime_params.empty()) {
        logos::probe::census("subst.structlit.site");
        std::unordered_map<std::string, std::string> flt;
        std::function<void(TypeRef, TypeRef)> walk = [&](TypeRef dt, TypeRef at) {
            if (!dt || !at) return;
            using K = LogosType::Kind;
            auto dk2 = dt.kind();
            if ((dk2 == K::Ref || dk2 == K::MutRef) &&
                (at.kind() == K::Ref || at.kind() == K::MutRef)) {
                std::string d(dt.lifetime()), a(at.lifetime());
                if (!d.empty() && !a.empty() && !flt.count(d)) flt.emplace(d, a);
                walk(dt.pointee(), at.pointee());
                return;
            }
            // Fat-pointer kinds: twin of the arm in structlit_lt_subst_
            // (sema_impl.hpp), first-wins here as every arm of this walk.
            if ((dk2 == K::Slice || dk2 == K::TraitObject || dk2 == K::DstRef) &&
                at.kind() == dk2) {
                std::string d(dt.lifetime()), a(at.lifetime());
                if (!d.empty() && !a.empty() && !flt.count(d)) flt.emplace(d, a);
                if (dk2 == K::Slice) walk(dt.elem(), at.elem());
                else {
                    auto da = dt.type_args(); auto aa = at.type_args();
                    for (size_t i = 0; i < da.size() && i < aa.size(); ++i) walk(da[i], aa[i]);
                }
                return;
            }
            if ((dk2 == K::Struct || dk2 == K::ZonedStruct || dk2 == K::Enum) &&
                at.kind() == dk2) {
                auto dl = dt.lifetime_args(); auto al = at.lifetime_args();
                for (size_t i = 0; i < dl.size() && i < al.size(); ++i)
                    if (!dl[i].empty() && !al[i].empty() && !flt.count(dl[i]))
                        flt.emplace(dl[i], al[i]);
                auto da = dt.type_args(); auto aa = at.type_args();
                for (size_t i = 0; i < da.size() && i < aa.size(); ++i) walk(da[i], aa[i]);
                return;
            }
            if (dk2 == K::Tuple && at.kind() == K::Tuple) {
                auto de = dt.tuple_elems(); auto ae = at.tuple_elems();
                for (size_t i = 0; i < de.size() && i < ae.size(); ++i) walk(de[i], ae[i]);
                return;
            }
        };
        for (auto& f : sinfo.fields)
            for (auto& [fname, fval] : fields)
                if (fval && fname == f.name) { walk(f.type, expr_type(fval)); break; }
        if (!flt.empty()) {
            ng_lt_args.resize(sinfo.lifetime_params.size());
            for (size_t i = 0; i < sinfo.lifetime_params.size(); ++i) {
                auto it = flt.find(sinfo.lifetime_params[i]);
                if (it != flt.end()) {
                    if (ng_lt_args[i] != it->second) logos::probe::census("subst.structlit.differs");
                    ng_lt_args[i] = it->second;
                }
            }
        }
        // A binder the VALUES did not spell stays EMPTY-BUT-PRESENT: "no fact
        // recorded" and "the fact is absent" are different, and only this
        // minting site can tell them apart. A type that never reached a mint
        // keeps zero args and still yields at the comparison.
        logos::probe::census("lit.mint.sized");
        ng_lt_args.assign(sinfo.lifetime_params.size(), std::string{});
        for (size_t i = 0; i < sinfo.lifetime_params.size(); ++i)
            if (auto it = flt.find(sinfo.lifetime_params[i]); it != flt.end())
                ng_lt_args[i] = it->second;
        // PROBE selflitv / selfv / selfvg — src/compiler/PROBES.md 2026-09-13a. The literal
        // spelled `Self` IS the impl self type: its VALUE regions must be a subtype of Self's,
        // and the literal then carries Self's. `hint_struct_type_` is the Self the top of this
        // function resolved (no second lookup of the name).
        if ((logos::probe::on("selflitv") || logos::probe::on("selfv") || logos::probe::on("selfvg") || logos::probe::on("selfvee") || logos::probe::on("selfveu")) && str_of(node.get(la::NAME.code)) == "Self") {
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
    }
    // B77: verify struct's `where 'a: 'b` against caller's outlives graph.
    check_struct_lit_outlives(std::string(sname),
                              sinfo.lifetime_params,
                              sinfo.lifetime_outlives,
                              ng_lt_args,
                              sinfo.fields,
                              fields);
    LogosTypeBuilder ng_t;
    ng_t.kind = slit_is_zoned
                ? LogosType::Kind::ZonedStruct : LogosType::Kind::Struct;
    ng_t.struct_name   = std::string(sname);
    if (auto rp = resolve_struct_pkg_(sname); !rp.empty()) ng_t.pkg_name = std::move(rp);
    ng_t.lifetime_args = std::move(ng_lt_args);
    TypeRef lit_result_type = pool_->alloc(std::move(ng_t));
    return builder().struct_lit(std::string(sname), std::move(fields), lit_result_type);
}

lir::LExprPtr SemaChecker::lower_index_place(TinyMapView node, bool is_mut) {
    if (code_of(node) != la::INDEX_READ) return nullptr;
    auto recv_node = map_of(node.get(la::RECEIVER.code));
    auto recv = lower_expr(recv_node);
    auto arr_type = expr_type(recv);
    if (TypeRef(arr_type).kind() != LogosType::Kind::Struct) return nullptr;

    auto type_name = concrete_struct_name(arr_type);
    auto base_name = std::string(TypeRef(arr_type).struct_name());
    // For `&mut f[i]` we need IndexMut; for `&f[i]`, Index is enough.
    const char* trait = is_mut ? "IndexMut" : "Index";
    bool has_trait = impls_.count(std::string(trait) + "::" + type_name) ||
                     (!base_name.empty() &&
                      impls_.count(std::string(trait) + "::" + base_name));
    if (!has_trait) {
        // `&mut f[i]` but no IndexMut impl — not a user index-place we can
        // honour. Fall through (generic path will diagnose / copy).
        return nullptr;
    }
    std::string method = is_mut ? "__index_mut" : "__index";
    auto mangled = type_name + method;
    const SemaFuncInfo* fit = nullptr;
    for (auto* c : find_func_candidates(mangled)) {
        if (c->param_types.size() == 2) { fit = c; break; }
    }
    if (!fit) return nullptr;

    lir::LExprPtr idx = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code)))
        : error_expr();
    widen_int_expr(idx, fit->param_types[1], builder());

    // Receiver address: for a plain variable take the REAL slot address
    // (`&mut f`), not a spilled copy — otherwise index_mut writes into a
    // temporary and the mutation is lost. Other receiver shapes fall back to
    // addr_of_temp (best-effort; a place chain like `g.h[i]` keeps the
    // pre-existing behaviour).
    auto self_ref_t = make_ref(is_mut, arr_type);
    lir::LExprPtr recv_ref = nullptr;
    if (code_of(recv_node) == la::VAR_REF) {
        auto var_name = std::string(str_of(recv_node.get(la::NAME.code)));
        recv_ref = builder().addr_of(var_name, self_ref_t);
    } else if (is_ref_like(TypeRef(arr_type).kind())) {
        // Receiver is already a reference/pointer to the struct — pass through.
        recv_ref = std::move(recv);
    } else {
        recv_ref = materialize_recv_ref(std::move(recv), is_mut, self_ref_t);
    }
    std::vector<lir::LExprPtr> args;
    args.push_back(std::move(recv_ref));
    args.push_back(std::move(idx));
    // index_mut returns `&mut Output` / index returns `&Output`; that IS the
    // place reference the caller wants — return it directly (no deref).
    return builder().call(
        fit->symbol_name.empty() ? mangled : fit->symbol_name,
        {}, std::move(args), fit->ret_type);
}

lir::LExprPtr SemaChecker::lower_index_read(TinyMapView node) {
    // Consume the mutable-use context (see `mut_place_ctx_`): an indexed place
    // written or borrowed mutably takes the IndexMut step, and its receiver
    // is itself a place in the same position (`*v[i] = x`, `b[i] = x` on a Box).
    bool mut_ctx = mut_place_ctx_;
    auto recv_node = map_of(node.get(la::RECEIVER.code));
    auto recv = mut_ctx ? lower_mut_place(recv_node) : lower_expr(recv_node);
    mut_place_ctx_ = false;
    auto arr_type = expr_type(recv);

    // Rust autoderef at the index position: peel `&&…` chains with REAL
    // deref-loads until at most one reference remains. A `&&[T; N]` receiver
    // used to fall into the single-level Ref handling, which takes the
    // POINTEE as the element — so `rr[1]` typed as the whole `&[T; N]`.
    while (arr_type &&
           (TypeRef(arr_type).kind() == LogosType::Kind::Ref ||
            TypeRef(arr_type).kind() == LogosType::Kind::MutRef) &&
           TypeRef(arr_type).pointee() &&
           (TypeRef(TypeRef(arr_type).pointee()).kind() == LogosType::Kind::Ref ||
            TypeRef(TypeRef(arr_type).pointee()).kind() == LogosType::Kind::MutRef)) {
        arr_type = TypeRef(arr_type).pointee();
        recv = builder().deref(std::move(recv), arr_type);
    }

    // `&[T]` is a fat-pointer SLICE value (Kind::Slice). A reference TO such a
    // slice (`&s` where `s: &[T]`, or a `&[T;N]` array-ref that decayed to a
    // slice and then got `&`-borrowed) arrives as `Ref→Slice`. The slice-index
    // codegen treats its slice operand as a POINTER into the `{data,len}` pair
    // (it GEPs field 0), and a `Ref→Slice` IS exactly that pointer — so we just
    // RETYPE the receiver to the pointee Slice (NOT a deref-load: loading the
    // 16-byte fat value and then GEP-ing it as a pointer segfaults). Without
    // this the element type resolves to the whole slice → `(&s)[i]` typed `&[T]`.
    if (arr_type &&
        (TypeRef(arr_type).kind() == LogosType::Kind::Ref ||
         TypeRef(arr_type).kind() == LogosType::Kind::MutRef) &&
        TypeRef(arr_type).pointee() &&
        TypeRef(TypeRef(arr_type).pointee()).kind() == LogosType::Kind::Slice) {
        arr_type = TypeRef(arr_type).pointee();
        builder().retype_expr(recv, arr_type);
    }

    // Range index `recv[lo..hi]` / `recv[lo..]` / `recv[..hi]` / `recv[..]` →
    // sub-slice via `slice_get_range` (Rust-parity slicing). Detect the
    // RANGE_EXPR index AST before lowering it as a scalar index.
    if (node.has_key(la::VALUE)) {
        auto idx_node = map_of(node.get(la::VALUE.code));
        if (code_of(idx_node) == la::RANGE_EXPR) {
            // Coerce receiver to `&[T]`: arrays decay to a slice; a `&[T]` /
            // str is used directly. (Vec → `.as_slice()` is a follow-up.)
            TypeRef elem = nullptr;
            auto rk = TypeRef(arr_type).kind();
            if (rk == LogosType::Kind::Slice) {
                elem = TypeRef(arr_type).elem();
            } else if (rk == LogosType::Kind::Array) {
                elem = TypeRef(arr_type).elem();
                // Array value → `&[T;N]` (addr-of) → `&[T]` (slice decay).
                recv = builder().addr_of_temp(std::move(recv), /*is_mut=*/false,
                                              make_ref(false, arr_type));
                try_coerce_array_ref_to_slice(recv, make_slice_type(elem ? elem : i32_t()));
            } else if ((rk == LogosType::Kind::Ref || rk == LogosType::Kind::MutRef) &&
                       TypeRef(arr_type).pointee() &&
                       TypeRef(TypeRef(arr_type).pointee()).kind() == LogosType::Kind::Slice) {
                elem = TypeRef(TypeRef(arr_type).pointee()).elem();
            }
            if (!elem || TypeRef(elem).kind() == LogosType::Kind::Error) {
                error(std::format("range index `[..]` requires a slice or array "
                      "receiver, got {}", type_str(arr_type)));
                return error_expr();
            }
            TypeRef i64t = prim(LogosType::Kind::I64);
            lir::LExprPtr lo = idx_node.has_key(la::LHS)
                ? lower_expr(map_of(idx_node.get(la::LHS.code))) : builder().lit_int(0, i64t);
            widen_int_expr(lo, i64t, builder());
            lir::LExprPtr hi = nullptr;
            if (idx_node.has_key(la::RHS)) {
                hi = lower_expr(map_of(idx_node.get(la::RHS.code)));
                widen_int_expr(hi, i64t, builder());
                bool inclusive = idx_node.has_key(la::INCLUSIVE) &&
                    !idx_node.get(la::INCLUSIVE.code).is_null() &&
                    idx_node.get(la::INCLUSIVE.code).as_value<uint8_t>() != 0;
                if (inclusive)  // ..=hi → hi+1 (clamped by slice_get_range)
                    hi = builder().bin_op("+", std::move(hi), builder().lit_int(1, i64t), i64t);
            } else {
                hi = builder().lit_int(INT64_MAX, i64t);  // open end → clamp to len
            }
            auto cands = find_func_candidates("slice_get_range");
            const SemaFuncInfo* sgr = cands.empty() ? nullptr : cands[0];
            if (!sgr) {
                error("range index: stdlib `slice_get_range` not in scope "
                      "(missing `use logos.lang.slice`)");
                return error_expr();
            }
            TypeRef ret_t = make_slice_type(elem);
            std::vector<lir::LExprPtr> args;
            args.push_back(std::move(recv));
            args.push_back(std::move(lo));
            args.push_back(std::move(hi));
            std::string sym = sgr->symbol_name.empty() ? std::string("slice_get_range")
                                                       : sgr->symbol_name;
            if (!sgr->type_params.empty())
                return finish_generic_call(sym, *sgr, {elem}, std::move(args));
            return builder().call(sym, {}, std::move(args), ret_t);
        }
    }

    lir::LExprPtr idx = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code)))
        : error_expr();

    // User-defined Index dispatch: `a[i]` for struct a where a impls
    // Index<Idx, Out> → `*(a.index(i))`. Tried before the built-in
    // integer-index check so a user impl can accept non-integer keys.
    // Rust autoderef at INDEX position: a struct receiver WITHOUT an Index
    // impl derefs through its Deref impl(s) until an indexable type appears
    // (`w[0]` where W: Deref<Vec<i64>> — adversarial #2 p15). Bounded walk
    // mirrors method-resolution autoderef.
    for (int _ad_step = 0; _ad_step < 4 &&
         TypeRef(arr_type).kind() == LogosType::Kind::Struct; ++_ad_step) {
        {
            auto _tn = concrete_struct_name(arr_type);
            auto _bn = std::string(TypeRef(arr_type).struct_name());
            bool _has_index = impls_.count("Index::" + _tn) ||
                              (!_bn.empty() && impls_.count("Index::" + _bn));
            if (_has_index) break;
        }
        bool deref_only = false;
        auto stepped = emit_generic_deref_call(std::move(recv), /*want_mut=*/mut_ctx,
                                               &deref_only);
        if (!stepped) break;     // recv (raw handle) still valid — no Deref impl
        if (mut_ctx && deref_only) { refuse_deref_only(arr_type); return error_expr(); }
        TypeRef rt2 = expr_type(*stepped);
        if (TypeRef(rt2).kind() == LogosType::Kind::Slice ||
            TypeRef(rt2).kind() == LogosType::Kind::TraitObject) {
            recv = std::move(*stepped);   // fat form IS the value
            arr_type = rt2;
            break;
        }
        TypeRef tgt = TypeRef(rt2).pointee() ? TypeRef(rt2).pointee() : rt2;
        recv = builder().deref(std::move(*stepped), tgt);
        arr_type = expr_type(recv);
    }
    if (TypeRef(arr_type).kind() == LogosType::Kind::Struct) {
        auto type_name = concrete_struct_name(arr_type);
        auto base_name = std::string(TypeRef(arr_type).struct_name());
        bool has_index = impls_.count("Index::" + type_name) ||
                         (!base_name.empty() && impls_.count("Index::" + base_name));
        // In a mutable-use position the step is `index_mut` (IndexMut), and
        // an `Index`-only type is not a writable place (E0594) — the same
        // refusal `lower_place_assign` gives a bare-variable receiver.
        const char* itr = mut_ctx ? "IndexMut" : "Index";
        bool has_trait = impls_.count(std::string(itr) + "::" + type_name) ||
                         (!base_name.empty() && impls_.count(std::string(itr) + "::" + base_name));
        if (has_index && mut_ctx && !has_trait) {
            error(std::format("cannot assign to index of '{}': type '{}' implements "
                              "`Index` but not `IndexMut`", type_str(arr_type),
                              type_name.empty() ? base_name : type_name));
            return error_expr();
        }
        if (has_index) {
            auto mangled = type_name + (mut_ctx ? "__index_mut" : "__index");
            auto ref_t = make_ref(mut_ctx, arr_type);
            // Pick the unique 2-param candidate (recv + idx). Widen
            // integer-literal idx to the formal type so `m[3]`-style
            // literal indexes match.
            const SemaFuncInfo* fit = nullptr;
            for (auto* c : find_func_candidates(mangled)) {
                if (c->param_types.size() == 2) { fit = c; break; }
            }
            if (fit) {
                widen_int_expr(idx, fit->param_types[1], builder());
                auto recv_ref = materialize_recv_ref(std::move(recv), mut_ctx, ref_t);
                std::vector<lir::LExprPtr> args;
                args.push_back(std::move(recv_ref));
                args.push_back(std::move(idx));
                auto call_e = builder().call(
                    fit->symbol_name.empty() ? mangled : fit->symbol_name,
                    {}, std::move(args), fit->ret_type);
                auto pointee = TypeRef(expr_type(call_e)).pointee()
                    ? TypeRef(expr_type(call_e)).pointee()
                    : error_t();
                return builder().deref(std::move(call_e), pointee);
            }
            // Generic-struct Index impl (`impl<T> Index for Vec<T>`): the
            // concrete `Vec$G1$i64__index` symbol doesn't exist at sema (only
            // the template), so the concrete-symbol path above misses. Route
            // `v[i]` → `*v.index(i)` through the normal method-call machinery
            // (which dispatches generic-struct methods + instantiates at mono).
            // Output = the impl's `Index<Idx, Output>` 2nd trait-arg, with the
            // struct's type-args substituted for the impl's type params.
            const SemaImplInfo* ii = nullptr;
            if (auto it = impls_.find(std::string(itr) + "::" + type_name); it != impls_.end()) ii = &it->second;
            else if (auto it2 = impls_.find(std::string(itr) + "::" + base_name); it2 != impls_.end()) ii = &it2->second;
            if (ii && ii->trait_type_args.size() >= 2) {
                SemaSubst subst;
                if (ii->target_typeref) {
                    auto pat = TypeRef(ii->target_typeref).type_args();
                    auto cur = TypeRef(arr_type).type_args();
                    for (size_t k = 0; k < pat.size() && k < cur.size(); ++k)
                        if (pat[k] && TypeRef(pat[k]).kind() == LogosType::Kind::TypeVar)
                            subst[std::string(TypeRef(pat[k]).type_var_name())] = cur[k];
                }
                TypeRef idx_t = subst_type_sema(ii->trait_type_args[0], subst);
                TypeRef out_t = subst_type_sema(ii->trait_type_args[1], subst);
                if (idx_t && TypeRef(idx_t).kind() != LogosType::Kind::TypeVar)
                    widen_int_expr(idx, idx_t, builder());
                lir::EMethodCall mc;
                mc.receiver = materialize_recv_ref(std::move(recv), mut_ctx, ref_t);
                mc.method = mut_ctx ? "index_mut" : "index";
                mc.args.push_back(std::move(idx));
                mc.vtable_index = -1;
                mc.resolved_type = "";
                auto call_e = builder().method_call_v(std::move(mc), make_ref(mut_ctx, out_t));
                return builder().deref(std::move(call_e), out_t);
            }
        }
    }

    if (!is_integer(expr_type(idx)))
        error(std::format("array index must be integer, got {}", type_str(expr_type(idx))));

    // Slice indexing: s[i] → ESliceIndex
    if (TypeRef(arr_type).kind() == LogosType::Kind::Slice) {
        auto elem = TypeRef(arr_type).elem() ? TypeRef(arr_type).elem() : error_t();
        return builder().slice_index(std::move(recv), std::move(idx), elem);
    }

    if (TypeRef(arr_type).kind() != LogosType::Kind::Array &&
        TypeRef(arr_type).kind() != LogosType::Kind::Ptr &&
        TypeRef(arr_type).kind() != LogosType::Kind::Ref &&
        TypeRef(arr_type).kind() != LogosType::Kind::MutRef &&
        TypeRef(arr_type).kind() != LogosType::Kind::Error) {
        error(std::format("index read: receiver is not an array, slice, or pointer (got {})",
              type_str(arr_type)));
    }
    if (TypeRef(arr_type).kind() == LogosType::Kind::Ptr && !inside_unsafe_) {
        error("index read through raw pointer requires unsafe context");
    }

    TypeRef elem = error_t();
    if (TypeRef(arr_type).kind() == LogosType::Kind::Array && TypeRef(arr_type).elem())  elem = TypeRef(arr_type).elem();
    if ((TypeRef(arr_type).kind() == LogosType::Kind::Ptr ||
         TypeRef(arr_type).kind() == LogosType::Kind::Ref ||
         TypeRef(arr_type).kind() == LogosType::Kind::MutRef) && TypeRef(arr_type).pointee()) {
        // For `&[T; N]` / `&mut [T; N]` / `*const [T; N]`, indexing through
        // the ref auto-derefs and yields the element type. Without this
        // step `a[0]` for `a: &[i32; N]` returns the whole array (with N
        // unresolved → "expected i32, got [i32; 0]").
        TypeRef pointee = TypeRef(arr_type).pointee();
        if (pointee.kind() == LogosType::Kind::Array && pointee.elem())
            elem = pointee.elem();
        else
            elem = pointee;
    }

    return builder().index_read(std::move(recv), std::move(idx), elem);
}

lir::LExprPtr SemaChecker::lower_arr_lit(TinyMapView node) {
    // Empty array literal `[]` / `&[]`. The element type is unknown from the
    // literal itself, but a `[T; N]` / `[T]` / `&[T]` annotation (or return
    // type) supplies it via hint_arr_elem_type_. Build an empty `[T; 0]` so a
    // borrow coerces to an empty slice `&[T]` (Rust `let s: &[u64] = &[];`).
    // Without a hint there is genuinely nothing to infer → keep the warning.
    bool no_items = !node.has_key(la::ITEMS);
    if (!no_items) {
        auto items0 = arr_of(node.get(la::ITEMS.code));
        no_items = (items0.size() == 0);
    }
    if (no_items) {
        if (hint_arr_elem_type_ &&
            TypeRef(hint_arr_elem_type_).kind() != LogosType::Kind::Error) {
            auto ty = make_array(hint_arr_elem_type_, 0);
            return builder().arr_lit(std::vector<lir::LExprPtr>{}, ty);
        }
        warn("empty array literal: element type unknown");
        return error_expr();
    }
    auto items = arr_of(node.get(la::ITEMS.code));
    std::vector<lir::LExprPtr> elems;
    for (uint64_t i = 0; i < items.size(); ++i)
        elems.push_back(lower_expr(map_of(items.get(i))));

    TypeRef elem_type = expr_type(elems[0]);
    // T0-5: a CONCRETE scalar element hint (a `&[i64]` formal / annotation,
    // via hint_arr_elem_type_) retypes an all-literal array's elements up
    // front. Slices alias raw memory, so the buffer must be BUILT at the
    // annotated width — the old flow let the lits default to i32 and the
    // permissive slice coercion read garbage at i64 stride.
    if (hint_arr_elem_type_) {
        auto hk = TypeRef(hint_arr_elem_type_).kind();
        bool hint_int = is_integer_kind(hk) &&
                        hk != LogosType::Kind::IntLit &&
                        hk != LogosType::Kind::Enum;
        bool hint_float = hk == LogosType::Kind::F32 ||
                          hk == LogosType::Kind::F64;
        if (hint_int || hint_float) {
            bool adoptable = true;
            size_t ei = 0;
            for (auto& e : elems) {
                auto k = TypeRef(expr_type(e)).kind();
                ++ei;
                if (k == LogosType::Kind::Error) continue;
                if (types_equal(expr_type(e), hint_arr_elem_type_)) continue;
                if (hint_int && k == LogosType::Kind::IntLit) {
                    if (auto v = get_intlit_value(e)) {
                        if (intlit_fits(*v, hk)) continue;
                        // Out-of-range literal for the annotated width is an
                        // error, not a silent fall-back to the i32 default
                        // (which the slice-aliasing check would then reject
                        // with a misleading type-mismatch).
                        error(std::format(
                            "array literal: element {}: value {} does not fit in {}",
                            ei - 1, *v, type_str(hint_arr_elem_type_)));
                        continue;
                    }
                }
                if (hint_float && k == LogosType::Kind::FloatLit) continue;
                adoptable = false;
                break;
            }
            if (adoptable) {
                for (auto& e : elems) {
                    auto k = TypeRef(expr_type(e)).kind();
                    if (k == LogosType::Kind::IntLit ||
                        k == LogosType::Kind::FloatLit)
                        builder().retype_expr(e, hint_arr_elem_type_);
                }
                elem_type = hint_arr_elem_type_;
            }
        }
    }
    // logos-core 1.4: a `[fn(...) -> R; N]` annotation lets a heterogeneous
    // array of distinct FnItems (each `fn-name` bare-ref) unify to a common
    // FnPtr. Each FnItem → FnPtr coerces via types_compatible; adopt the
    // hint as the element type so the homogeneity check below sees FnPtr,
    // not the per-element FnItem.
    bool fnptr_elem_hint = false;
    if (hint_arr_elem_type_ &&
        TypeRef(hint_arr_elem_type_).kind() == LogosType::Kind::FnPtr) {
        bool all_coerce = true;
        for (auto& e : elems) {
            TypeRef et = expr_type(e);
            if (TypeRef(et).kind() == LogosType::Kind::Error) continue;
            if (types_compatible(et, hint_arr_elem_type_)) continue;
            all_coerce = false; break;
        }
        if (all_coerce) {
            elem_type = hint_arr_elem_type_;
            fnptr_elem_hint = true;
            for (auto& e : elems) {
                if (!e || TypeRef(expr_type(e)).kind() == LogosType::Kind::Error)
                    continue;
                if (types_equal(expr_type(e), hint_arr_elem_type_)) continue;
                e = builder().cast(std::move(e), hint_arr_elem_type_);
            }
        }
    }
    // `[&arr3, &arr5]` under a `[&[T]; N]` annotation: each element decays to
    // the hinted slice, exactly as it would at any other expected-type
    // position. Without this the elements keep their per-length types and the
    // literal is heterogeneous by construction.
    if (hint_arr_elem_type_ &&
        (TypeRef(hint_arr_elem_type_).kind() == LogosType::Kind::Slice ||
         TypeRef(hint_arr_elem_type_).kind() == LogosType::Kind::UnsizedSlice)) {
        bool any = false;
        for (auto& e : elems) {
            if (!e || TypeRef(expr_type(e)).kind() == LogosType::Kind::Error)
                continue;
            if (try_coerce_array_ref_to_slice(e, hint_arr_elem_type_)) any = true;
        }
        // elem_type was derived from element 0 BEFORE the decay; recompute it
        // or the literal keeps the pre-decay per-length type and every other
        // element mismatches against it.
        if (any) elem_type = hint_arr_elem_type_;
    }
    // g6b: a `[&dyn Trait; N]` annotation lets a HETEROGENEOUS array of distinct
    // `&Concrete` refs unify to `&dyn Trait`. When the expected element type is
    // known and every element coerces to it (with at least one needing the
    // `&Concrete → &dyn` unsize), adopt the hint as the element type and skip
    // the homogeneity checks below — codegen builds each fat pointer per-element.
    bool dyn_elem_hint = false;
    if (hint_arr_elem_type_ &&
        TypeRef(hint_arr_elem_type_).kind() != LogosType::Kind::Error) {
        TypeRef he = hint_arr_elem_type_;
        // ref_arg_satisfies_dyn wants the bare TraitObject; `he` is the ref form
        // `&dyn Trait` (Ref→TraitObject) when from a `[&dyn Trait; N]` annotation.
        TypeRef he_dyn = he;
        if ((TypeRef(he).kind() == LogosType::Kind::Ref ||
             TypeRef(he).kind() == LogosType::Kind::MutRef) &&
            TypeRef(he).pointee() &&
            TypeRef(TypeRef(he).pointee()).kind() == LogosType::Kind::TraitObject)
            he_dyn = TypeRef(he).pointee();
        // A `[&dyn Trait; N]` hint (element type is a TraitObject) wants every
        // element coerced to the fat `&dyn` pointer. Engage when each element is
        // compatible with, or unsize-coercible to, that dyn element type.
        bool he_is_dyn = TypeRef(he_dyn).kind() == LogosType::Kind::TraitObject;
        if (he_is_dyn) {
            bool all_coerce = true;
            for (auto& e : elems) {
                TypeRef et = expr_type(e);
                if (TypeRef(et).kind() == LogosType::Kind::Error) continue;
                if (types_compatible(et, he) || ref_arg_satisfies_dyn(et, he_dyn)) continue;
                all_coerce = false; break;
            }
            if (all_coerce) {
                elem_type = he;
                dyn_elem_hint = true;
                // Wrap each not-already-`&dyn` element in an explicit
                // dyn-coercion cast so codegen builds the fat pointer (vtable)
                // per element AND mono's scan collects the concrete coercion
                // target (so its blanket method instantiates). Adopting the hint
                // TYPE alone leaves a thin `&Concrete` in the `&dyn` slot —
                // reading the (absent) vtable SIGSEGVs. The explicit-cast form
                // (`&x as &dyn`) already produced this ECast; the implicit form
                // (bare `&x` under a `[&dyn; N]` hint) did not.
                for (auto& e : elems) {
                    if (!e || TypeRef(expr_type(e)).kind() == LogosType::Kind::Error)
                        continue;
                    if (types_compatible(expr_type(e), he)) continue;  // already &dyn
                    e = builder().cast(std::move(e), he);
                }
            }
        }
    }
    for (uint64_t i = 1; !dyn_elem_hint && !fnptr_elem_hint && i < elems.size(); ++i) {
        auto t = expr_type(elems[i]);
        if (TypeRef(t).kind() != LogosType::Kind::Error && TypeRef(elem_type).kind() != LogosType::Kind::Error) {
            if (!types_compatible(t, elem_type) && !types_compatible(elem_type, t)) {
                { auto [es, gs] = type_str_pair(t, elem_type);
                  error(std::format("array literal: element {} has type {}, expected {}",
                      i, es, gs)); }
            } else {
                // If the concrete element type is narrow and this element is IntLit, check range.
                if (TypeRef(t).kind() == LogosType::Kind::IntLit &&
                    TypeRef(elem_type).kind() != LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(elems[i]))
                        if (!intlit_fits(*v, TypeRef(elem_type).kind()))
                            error(std::format("array literal: element {}: value {} does not fit in {}",
                                  i, *v, type_str(elem_type)));
                // Check array literal elements against narrow nested array element types.
                if (TypeRef(elem_type).kind() == LogosType::Kind::Array && TypeRef(elem_type).elem() &&
                    TypeRef(t).kind() == LogosType::Kind::Array) {
                    auto vr = expr_ref_of(elems[i]);
                    if (vr.kind() == lir_schema::expr::Code::ArrLit) {
                        lir_view::EArrLitView al{vr};
                        for (uint64_t ei = 0; ei < al.count(); ++ei) {
                            auto el = al.elem(ei);
                            if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(el))
                                    if (!intlit_fits(*v, TypeRef(elem_type).elem().kind()))
                                        error(std::format("array literal: element {}: sub-element {}: value {} does not fit in {}",
                                              i, ei, *v, type_str(TypeRef(elem_type).elem())));
                        }
                    }
                }
                // Check tuple literal elements against narrow nested tuple element types.
                if (TypeRef(elem_type).kind() == LogosType::Kind::Tuple && TypeRef(t).kind() == LogosType::Kind::Tuple) {
                    auto vr = expr_ref_of(elems[i]);
                    if (vr.kind() == lir_schema::expr::Code::TupleLit) {
                        lir_view::ETupleLitView tl{vr};
                        uint64_t ei = 0;
                        tl.each_elem([&](lir_view::ExprRef el) {
                            if (ei >= TypeRef(elem_type).tuple_elems().size()) { ++ei; return; }
                            if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(el))
                                    if (TypeRef(elem_type).tuple_elems()[ei] && !intlit_fits(*v, TypeRef(TypeRef(elem_type).tuple_elems()[ei]).kind()))
                                        error(std::format("array literal: element {}: tuple element {}: value {} does not fit in {}",
                                              i, ei, *v, type_str(TypeRef(elem_type).tuple_elems()[ei])));
                            if (TypeRef(elem_type).tuple_elems()[ei] && TypeRef(TypeRef(elem_type).tuple_elems()[ei]).kind() == LogosType::Kind::Array &&
                                TypeRef(TypeRef(elem_type).tuple_elems()[ei]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                                el.kind() == lir_schema::expr::Code::ArrLit) {
                                lir_view::EArrLitView ial{el};
                                for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                                    auto iel = ial.elem(ii);
                                    if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(iel))
                                            if (!intlit_fits(*v, TypeRef(TypeRef(elem_type).tuple_elems()[ei]).elem().kind()))
                                                error(std::format("array literal: element {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                      i, ei, ii, *v, type_str(TypeRef(TypeRef(elem_type).tuple_elems()[ei]).elem())));
                                }
                            }
                            if (TypeRef(elem_type).tuple_elems()[ei] && TypeRef(TypeRef(elem_type).tuple_elems()[ei]).kind() == LogosType::Kind::Tuple &&
                                el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                                el.kind() == lir_schema::expr::Code::TupleLit) {
                                lir_view::ETupleLitView itl{el};
                                uint64_t ii = 0;
                                itl.each_elem([&](lir_view::ExprRef iel) {
                                    if (ii >= TypeRef(TypeRef(elem_type).tuple_elems()[ei]).tuple_elems().size()) { ++ii; return; }
                                    if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(iel))
                                            if (TypeRef(TypeRef(elem_type).tuple_elems()[ei]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(elem_type).tuple_elems()[ei]).tuple_elems()[ii]).kind()))
                                                error(std::format("array literal: element {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                      i, ei, ii, *v, type_str(TypeRef(TypeRef(elem_type).tuple_elems()[ei]).tuple_elems()[ii])));
                                    ++ii;
                                });
                            }
                            ++ei;
                        });
                    }
                }
                elem_type = unify_numeric(elem_type, t);
            }
        }
    }
    // Element 0 retroactive check: the loop above only checks elements 1+.
    // If a later element has a concrete narrow type, element 0 (which set
    // elem_type initially) was never range-checked against it.
    // Find the first concrete anchor from elements 1+ and check element 0.
    if (!dyn_elem_hint && elems.size() > 1) {
        // Locate the first element whose type is concrete (not purely IntLit-typed).
        TypeRef anchor = nullptr;
        for (size_t i = 1; i < elems.size() && !anchor; ++i) {
            TypeRef ti = expr_type(elems[i]);
            if (TypeRef(ti).kind() != LogosType::Kind::IntLit &&
                !(TypeRef(ti).kind() == LogosType::Kind::Array && TypeRef(ti).elem() &&
                  TypeRef(ti).elem().kind() == LogosType::Kind::IntLit))
                anchor = ti;
        }
        if (anchor) {
            auto e = elems[0];
            auto t0 = expr_type(elems[0]);
            // Scalar IntLit at element 0.
            if (TypeRef(t0).kind() == LogosType::Kind::IntLit)
                if (auto v = get_intlit_value(e))
                    if (!intlit_fits(*v, TypeRef(anchor).kind()))
                        error(std::format("array literal: element 0: value {} does not fit in {}",
                              *v, type_str(anchor)));
            // Array literal at element 0 (e.g. [[1,200,3], concrete_arr]).
            if (TypeRef(anchor).kind() == LogosType::Kind::Array && TypeRef(anchor).elem() &&
                TypeRef(t0).kind() == LogosType::Kind::Array) {
                auto vr = expr_ref_of(e);
                if (vr.kind() == lir_schema::expr::Code::ArrLit) {
                    lir_view::EArrLitView al{vr};
                    for (uint64_t ei = 0; ei < al.count(); ++ei) {
                        auto el = al.elem(ei);
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (!intlit_fits(*v, TypeRef(anchor).elem().kind()))
                                    error(std::format("array literal: element 0: sub-element {}: value {} does not fit in {}",
                                          ei, *v, type_str(TypeRef(anchor).elem())));
                    }
                }
            }
            // Tuple literal at element 0 (tuple elements, including nested array/tuple).
            if (TypeRef(anchor).kind() == LogosType::Kind::Tuple && TypeRef(t0).kind() == LogosType::Kind::Tuple) {
                auto vr = expr_ref_of(e);
                if (vr.kind() == lir_schema::expr::Code::TupleLit) {
                    lir_view::ETupleLitView tl{vr};
                    uint64_t ei = 0;
                    tl.each_elem([&](lir_view::ExprRef el) {
                        if (ei >= TypeRef(anchor).tuple_elems().size()) { ++ei; return; }
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (TypeRef(anchor).tuple_elems()[ei] && !intlit_fits(*v, TypeRef(TypeRef(anchor).tuple_elems()[ei]).kind()))
                                    error(std::format("array literal: element 0: tuple element {}: value {} does not fit in {}",
                                          ei, *v, type_str(TypeRef(anchor).tuple_elems()[ei])));
                        if (TypeRef(anchor).tuple_elems()[ei] && TypeRef(TypeRef(anchor).tuple_elems()[ei]).kind() == LogosType::Kind::Array &&
                            TypeRef(TypeRef(anchor).tuple_elems()[ei]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                            el.kind() == lir_schema::expr::Code::ArrLit) {
                            lir_view::EArrLitView ial{el};
                            for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                                auto iel = ial.elem(ii);
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (!intlit_fits(*v, TypeRef(TypeRef(anchor).tuple_elems()[ei]).elem().kind()))
                                            error(std::format("array literal: element 0: tuple element {}: array element {}: value {} does not fit in {}",
                                                  ei, ii, *v, type_str(TypeRef(TypeRef(anchor).tuple_elems()[ei]).elem())));
                            }
                        }
                        if (TypeRef(anchor).tuple_elems()[ei] && TypeRef(TypeRef(anchor).tuple_elems()[ei]).kind() == LogosType::Kind::Tuple &&
                            el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                            el.kind() == lir_schema::expr::Code::TupleLit) {
                            lir_view::ETupleLitView itl{el};
                            uint64_t ii = 0;
                            itl.each_elem([&](lir_view::ExprRef iel) {
                                if (ii >= TypeRef(TypeRef(anchor).tuple_elems()[ei]).tuple_elems().size()) { ++ii; return; }
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (TypeRef(TypeRef(anchor).tuple_elems()[ei]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(anchor).tuple_elems()[ei]).tuple_elems()[ii]).kind()))
                                            error(std::format("array literal: element 0: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                  ei, ii, *v, type_str(TypeRef(TypeRef(anchor).tuple_elems()[ei]).tuple_elems()[ii])));
                                ++ii;
                            });
                        }
                        ++ei;
                    });
                }
            }
        }
    }
    // For IntLit element type: upgrade to i64 if any value overflows i32.
    // Keep IntLit (don't collapse to i32) so that annotation-based coercion
    // ([i64; N] = [1, 2, 3]) can use types_compatible([IntLit;N], [i64;N]) → true.
    if (TypeRef(elem_type).kind() == LogosType::Kind::IntLit) {
        bool needs_i64 = false;
        for (const auto& elem : elems) {
            if (auto v = get_intlit_value(elem))
                if (*v > (int64_t)INT32_MAX || *v < (int64_t)INT32_MIN)
                    { needs_i64 = true; break; }
        }
        if (needs_i64) elem_type = prim(LogosType::Kind::I64);
        // else: leave as IntLit — mlir_gen will see the annotation type
    }

    // Const-pack expansion: `[N...]` over a `<const N...: T>` pack. Build
    // `[T; sizeof...(N)]` symbolic-length array; mono will replace the single
    // PackExpand element with one lit_int per pack member.
    if (elems.size() == 1 &&
        expr_ref_of(elems[0]).kind() == lir_schema::expr::Code::PackExpand &&
        TypeRef(elem_type).kind() == LogosType::Kind::ConstVar) {
        std::string pack_name(TypeRef(elem_type).type_var_name());
        TypeRef under = TypeRef(elem_type).pointee();
        if (!under) under = prim(LogosType::Kind::I64);
        LogosTypeBuilder ab; ab.kind = LogosType::Kind::Array;
        ab.elem = under;
        ab.arr_size = 0;
        ab.arr_size_var = std::string(ARR_LEN_PACK_PFX) + pack_name;
        TypeRef arr_t = pool_->alloc(std::move(ab));
        return builder().arr_lit(std::move(elems), arr_t);
    }

    auto ty = make_array(elem_type, elems.size());
    return builder().arr_lit(std::move(elems), ty);
}

// List comprehension:  [elem_expr for x in iter_expr (if guard)?]
// Desugars to a block expression that creates a Vec<T>, iterates over
// iter_expr, optionally filters by guard, and pushes elem_expr into the Vec.
// Requires `use logos.mem.collections.vec;` in scope.
// Iterator support: array / slice (via SForEach); generic iterator path
// (types with .next() returning Option<T>) is deferred.
lir::LExprPtr SemaChecker::lower_list_comp(TinyMapView node) {
    auto var_name = str_of(node.get(la::NAME.code));

    lir::LExprPtr iter = node.has_key(la::ITER)
        ? lower_expr(map_of(node.get(la::ITER.code))) : error_expr();
    TypeRef iter_type = expr_type(iter);

    // Only array/slice iteration supported for now.
    TypeRef elem_type = nullptr;
    int64_t arr_size = 0;
    bool is_slice = false;
    if (TypeRef(iter_type).kind() == LogosType::Kind::Array) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
        arr_size  = (int64_t)TypeRef(iter_type).arr_size();
    } else if (TypeRef(iter_type).kind() == LogosType::Kind::Slice) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
        is_slice  = true;
    } else {
        error(std::format(
            "list comprehension: only array/slice iteration supported (got {})",
            type_str(iter_type)));
        return error_expr();
    }

    // Require Vec<T> available (via `use std.vec`).
    {
        auto [vpkg, vsi] = find_struct_by_name("Vec");
        if (!vsi) {
            error("list comprehension requires `use logos.mem.collections.vec;`");
            return error_expr();
        }
    }
    auto* vec_new_fi = find_generic_func("vec_new");
    if (!vec_new_fi) {
        error("list comprehension: vec_new not found; add `use logos.mem.collections.vec;`");
        return error_expr();
    }

    TypeRef vec_t = make_synth_generic_struct("Vec", {elem_type});

    std::string vec_var = "__lc_v_" + std::to_string(tmp_var_count_++);

    // SLet: let mut vec_var: Vec<T> = vec_new::<T>();
    // Use symbol_name (may include __g__... suffix for method-level generics).
    std::string vec_new_sym = vec_new_fi->symbol_name.empty() ? "vec_new"
                                                              : vec_new_fi->symbol_name;
    auto call_new = builder().call(vec_new_sym, {elem_type}, {}, vec_t);
    lir::SLet let_v;
    let_v.name   = vec_var;
    let_v.type   = vec_t;
    let_v.is_mut = true;
    let_v.value  = std::move(call_new);

    // Lower VALUE + optional GUARD with var_name in scope.
    push_scope();
    define(vec_var, vec_t, true);
    define(std::string(var_name), elem_type, false);
    auto elem_expr = lower_expr(map_of(node.get(la::VALUE.code)));
    lir::LExprPtr guard_expr = nullptr;
    if (node.has_key(la::GUARD))
        guard_expr = lower_expr(map_of(node.get(la::GUARD.code)));
    pop_scope();

    // Call Vec::push(&mut vec_var, elem) as a direct ECall.
    // Emit with callee "Vec__push" and type_args=[elem_type]; mono_clone will
    // rewrite to the struct-specialized name (e.g. Vec$G1$i32__push).
    auto recv = builder().addr_of(vec_var, make_ptr(true, vec_t));
    std::vector<lir::LExprPtr> push_args;
    push_args.push_back(std::move(recv));
    push_args.push_back(std::move(elem_expr));
    auto push_call = builder().call("Vec__push", {elem_type}, std::move(push_args), void_t());

    lir::SExprStmt push_stmt;
    push_stmt.expr = std::move(push_call);

    std::vector<lir_view::StmtRef> loop_body;
    if (guard_expr) {
        lir::SIf sif;
        sif.cond = std::move(guard_expr);
        std::vector<lir_view::StmtRef> then_blk;
        then_blk.push_back(make_stmt_emit(node_line_, std::move(push_stmt)));
        sif.then_ = lir_mirror_block(*cur_prog_, then_blk);
        loop_body.push_back(make_stmt_emit(node_line_, std::move(sif)));
    } else {
        loop_body.push_back(make_stmt_emit(node_line_, std::move(push_stmt)));
    }

    lir::SForEach sfe;
    sfe.var       = std::string(var_name);
    sfe.iter      = std::move(iter);
    sfe.elem_type = elem_type;
    sfe.arr_size  = arr_size;
    sfe.is_slice  = is_slice;
    sfe.body      = lir_mirror_block(*cur_prog_, loop_body);

    std::vector<lir_view::StmtRef> outer;
    outer.push_back(make_stmt_emit(node_line_, std::move(let_v)));
    outer.push_back(make_stmt_emit(node_line_, std::move(sfe)));

    auto result = builder().var_ref(vec_var, vec_t);
    return builder().block_expr(lir_mirror_block(*cur_prog_, outer), std::move(result), vec_t);
}

// Map comprehension:  {kexpr: vexpr for x in iter_expr (if guard)?}
// Desugars to a block that creates a HashMap<K,V>, iterates over iter_expr,
// optionally filters by guard, and inserts (kexpr, vexpr) pairs.
// Requires `use logos.mem.collections.hashmap;` in scope.
lir::LExprPtr SemaChecker::lower_map_comp(TinyMapView node) {
    auto var_name = str_of(node.get(la::NAME.code));

    lir::LExprPtr iter = node.has_key(la::ITER)
        ? lower_expr(map_of(node.get(la::ITER.code))) : error_expr();
    TypeRef iter_type = expr_type(iter);

    TypeRef elem_type = nullptr;
    int64_t arr_size = 0;
    bool is_slice = false;
    if (TypeRef(iter_type).kind() == LogosType::Kind::Array) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
        arr_size  = (int64_t)TypeRef(iter_type).arr_size();
    } else if (TypeRef(iter_type).kind() == LogosType::Kind::Slice) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
        is_slice  = true;
    } else {
        error(std::format(
            "map comprehension: only array/slice iteration supported (got {})",
            type_str(iter_type)));
        return error_expr();
    }

    {
        auto [hmpkg, hmsi] = find_struct_by_name("HashMap");
        if (!hmsi) {
            error("map comprehension requires `use logos.mem.collections.hashmap;`");
            return error_expr();
        }
    }
    auto* hm_new_fi = find_generic_func("hashmap_new");
    if (!hm_new_fi) {
        error("map comprehension: hashmap_new not found; add `use logos.mem.collections.hashmap;`");
        return error_expr();
    }

    std::string hm_var = "__mc_m_" + std::to_string(tmp_var_count_++);

    push_scope();
    define(std::string(var_name), elem_type, false);
    auto key_expr_body = lower_expr(map_of(node.get(la::KEY.code)));
    auto val_expr_body = lower_expr(map_of(node.get(la::VALUE.code)));
    lir::LExprPtr guard_body = nullptr;
    if (node.has_key(la::GUARD))
        guard_body = lower_expr(map_of(node.get(la::GUARD.code)));
    pop_scope();

    TypeRef k_type = expr_type(key_expr_body);
    TypeRef v_type = expr_type(val_expr_body);
    TypeRef hm_t = make_generic_struct("HashMap", {k_type, v_type});

    std::string hm_new_sym = hm_new_fi->symbol_name.empty() ? "hashmap_new"
                                                            : hm_new_fi->symbol_name;
    auto call_new = builder().call(hm_new_sym, {k_type, v_type}, {}, hm_t);
    lir::SLet let_m;
    let_m.name   = hm_var;
    let_m.type   = hm_t;
    let_m.is_mut = true;
    let_m.value  = std::move(call_new);

    // HashMap::insert(&mut hm, key, val) — unsafe method, emitted as direct ECall
    // "HashMap__insert" so mono_clone rewrites to HashMap$G1$..$G2$..__insert.
    auto recv = builder().addr_of(hm_var, make_ptr(true, hm_t));
    std::vector<lir::LExprPtr> ins_args;
    ins_args.push_back(std::move(recv));
    ins_args.push_back(std::move(key_expr_body));
    ins_args.push_back(std::move(val_expr_body));
    auto ins_call = builder().call("HashMap__insert", {k_type, v_type}, std::move(ins_args), void_t());

    lir::SExprStmt ins_stmt;
    ins_stmt.expr = std::move(ins_call);

    std::vector<lir_view::StmtRef> loop_body;
    if (guard_body) {
        lir::SIf sif;
        sif.cond = std::move(guard_body);
        std::vector<lir_view::StmtRef> then_blk;
        then_blk.push_back(make_stmt_emit(node_line_, std::move(ins_stmt)));
        sif.then_ = lir_mirror_block(*cur_prog_, then_blk);
        loop_body.push_back(make_stmt_emit(node_line_, std::move(sif)));
    } else {
        loop_body.push_back(make_stmt_emit(node_line_, std::move(ins_stmt)));
    }

    lir::SForEach sfe;
    sfe.var       = std::string(var_name);
    sfe.iter      = std::move(iter);
    sfe.elem_type = elem_type;
    sfe.arr_size  = arr_size;
    sfe.is_slice  = is_slice;
    sfe.body      = lir_mirror_block(*cur_prog_, loop_body);

    std::vector<lir_view::StmtRef> outer;
    outer.push_back(make_stmt_emit(node_line_, std::move(let_m)));
    outer.push_back(make_stmt_emit(node_line_, std::move(sfe)));

    auto result = builder().var_ref(hm_var, hm_t);
    return builder().block_expr(lir_mirror_block(*cur_prog_, outer), std::move(result), hm_t);
}

// Writ list comprehension:  @[expr for x in iter_expr (if guard)?]
// Desugars to a block that builds a Writ whose root is an
// ObjectArray of AnyVals, iterating over iter_expr and optionally
// filtering by guard.  Element expression must evaluate to AnyVal
// (user coerces scalars explicitly via AnyVal::embed_i24 etc.).
// Requires `use logos.mem.writ.ctr;` in scope.
lir::LExprPtr SemaChecker::lower_writ_list_comp(TinyMapView node) {
    auto var_name = str_of(node.get(la::NAME.code));

    lir::LExprPtr iter = node.has_key(la::ITER)
        ? lower_expr(map_of(node.get(la::ITER.code))) : error_expr();
    TypeRef iter_type = expr_type(iter);

    // Short-circuit on upstream error to avoid cascading diagnostics.
    if (TypeRef(iter_type).kind() == LogosType::Kind::Error)
        return error_expr();

    TypeRef elem_type = nullptr;
    int64_t arr_size = 0;
    bool is_slice = false;
    if (TypeRef(iter_type).kind() == LogosType::Kind::Array) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
        arr_size  = (int64_t)TypeRef(iter_type).arr_size();
    } else if (TypeRef(iter_type).kind() == LogosType::Kind::Slice) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
        is_slice  = true;
    } else {
        error(std::format(
            "writ list comprehension: only array/slice iteration supported (got {})",
            type_str(iter_type)));
        return error_expr();
    }

    // writ builder: yields Rc<Writ> (see lang.writ.comp_builder).
    auto new_cands  = find_func_candidates("writ_list_comp_new");
    auto push_cands = find_func_candidates("writ_list_comp_push");
    const SemaFuncInfo* new_fi  = nullptr;
    const SemaFuncInfo* push_fi = nullptr;
    for (auto* fi : new_cands)  if (fi->param_types.size() == 1) { new_fi  = fi; break; }
    for (auto* fi : push_cands) if (fi->param_types.size() == 2) { push_fi = fi; break; }
    if (!new_fi || !push_fi) {
        error("writ list comprehension requires `use logos.lang.writ.comp_builder;`");
        return error_expr();
    }

    // The container type is whatever the builder returns (Rc<Writ>).
    TypeRef ctr_t = new_fi->ret_type;

    std::string ctr_var = "__hlc_c_" + std::to_string(tmp_var_count_++);

    push_scope();
    define(ctr_var, ctr_t, true);
    define(std::string(var_name), elem_type, false);
    auto val_expr_body = lower_expr(map_of(node.get(la::VALUE.code)));
    lir::LExprPtr guard_body = nullptr;
    if (node.has_key(la::GUARD))
        guard_body = lower_expr(map_of(node.get(la::GUARD.code)));
    pop_scope();

    // Coerce VALUE to AnyVal (no-op if already AnyVal).
    val_expr_body = coerce_to_writ_anyval(
        std::move(val_expr_body), ctr_var, ctr_t,
        "writ list comprehension element");
    if (!val_expr_body || TypeRef(expr_type(val_expr_body)).kind() == LogosType::Kind::Error)
        return error_expr();

    // Guard must be Bool; any other type (including Error) is rejected here to
    // avoid cascading diagnostics and to prevent an MLIR verification crash
    // from feeding a non-i1 value into cf.cond_br.
    if (guard_body) {
        auto gk = expr_type(guard_body) ? TypeRef(expr_type(guard_body)).kind()
                                   : LogosType::Kind::Error;
        if (gk == LogosType::Kind::Error)
            return error_expr();
        if (gk != LogosType::Kind::Bool) {
            error(std::format(
                "writ list comprehension: guard must be bool (got {})",
                type_str(expr_type(guard_body))));
            return error_expr();
        }
    }

    // SLet: let mut __hlc_c = writ_list_comp_new(128);
    std::string new_sym = new_fi->symbol_name.empty() ? "writ_list_comp_new"
                                                      : new_fi->symbol_name;
    std::vector<lir::LExprPtr> new_args;
    int64_t cap_hint = arr_size > 0 ? (arr_size * 8 + 128) : 128;
    new_args.push_back(builder().lit_int(cap_hint, prim(LogosType::Kind::I64)));
    auto call_new = builder().call(new_sym, {}, std::move(new_args), ctr_t);
    lir::SLet let_c;
    let_c.name   = ctr_var;
    let_c.type   = ctr_t;
    let_c.is_mut = true;
    let_c.value  = std::move(call_new);

    // writ_list_comp_push(&mut __hlc_c, val);
    std::string push_sym = push_fi->symbol_name.empty() ? "writ_list_comp_push"
                                                        : push_fi->symbol_name;
    // push takes `&Rc<Writ>` (shared) — was `&mut Writ`.
    auto recv = builder().addr_of(ctr_var, make_ref(false, ctr_t));
    std::vector<lir::LExprPtr> push_args;
    push_args.push_back(std::move(recv));
    push_args.push_back(std::move(val_expr_body));
    auto push_call = builder().call(push_sym, {}, std::move(push_args), void_t());

    lir::SExprStmt push_stmt;
    push_stmt.expr = std::move(push_call);

    std::vector<lir_view::StmtRef> loop_body;
    if (guard_body) {
        lir::SIf sif;
        sif.cond = std::move(guard_body);
        std::vector<lir_view::StmtRef> then_blk;
        then_blk.push_back(make_stmt_emit(node_line_, std::move(push_stmt)));
        sif.then_ = lir_mirror_block(*cur_prog_, then_blk);
        loop_body.push_back(make_stmt_emit(node_line_, std::move(sif)));
    } else {
        loop_body.push_back(make_stmt_emit(node_line_, std::move(push_stmt)));
    }

    lir::SForEach sfe;
    sfe.var       = std::string(var_name);
    sfe.iter      = std::move(iter);
    sfe.elem_type = elem_type;
    sfe.arr_size  = arr_size;
    sfe.is_slice  = is_slice;
    sfe.body      = lir_mirror_block(*cur_prog_, loop_body);

    std::vector<lir_view::StmtRef> outer;
    outer.push_back(make_stmt_emit(node_line_, std::move(let_c)));
    outer.push_back(make_stmt_emit(node_line_, std::move(sfe)));

    auto result = builder().var_ref(ctr_var, ctr_t);
    return builder().block_expr(lir_mirror_block(*cur_prog_, outer), std::move(result), ctr_t);
}

// Writ map comprehension:  @{kexpr: vexpr for x in iter (if guard)?}
// v1: string keys only (`str`); values must be AnyVal.
// Requires `use logos.mem.writ.ctr;` in scope.
lir::LExprPtr SemaChecker::lower_writ_map_comp(TinyMapView node) {
    auto var_name = str_of(node.get(la::NAME.code));

    lir::LExprPtr iter = node.has_key(la::ITER)
        ? lower_expr(map_of(node.get(la::ITER.code))) : error_expr();
    TypeRef iter_type = expr_type(iter);

    // Short-circuit on upstream error to avoid cascading diagnostics.
    if (TypeRef(iter_type).kind() == LogosType::Kind::Error)
        return error_expr();

    TypeRef elem_type = nullptr;
    int64_t arr_size = 0;
    bool is_slice = false;
    if (TypeRef(iter_type).kind() == LogosType::Kind::Array) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
        arr_size  = (int64_t)TypeRef(iter_type).arr_size();
    } else if (TypeRef(iter_type).kind() == LogosType::Kind::Slice) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
        is_slice  = true;
    } else {
        error(std::format(
            "writ map comprehension: only array/slice iteration supported (got {})",
            type_str(iter_type)));
        return error_expr();
    }

    // writ builder: yields Rc<Writ> (see lang.writ.comp_builder).
    auto new_cands = find_func_candidates("writ_map_comp_new");
    auto put_cands = find_func_candidates("writ_map_comp_put");
    const SemaFuncInfo* new_fi = nullptr;
    const SemaFuncInfo* put_fi = nullptr;
    for (auto* fi : new_cands) if (fi->param_types.size() == 2) { new_fi = fi; break; }
    for (auto* fi : put_cands) if (fi->param_types.size() == 3) { put_fi = fi; break; }
    if (!new_fi || !put_fi) {
        error("writ map comprehension requires `use logos.lang.writ.comp_builder;`");
        return error_expr();
    }

    // The container type is whatever the builder returns (Rc<Writ>).
    TypeRef ctr_t = new_fi->ret_type;

    std::string ctr_var = "__hmc_c_" + std::to_string(tmp_var_count_++);

    push_scope();
    define(ctr_var, ctr_t, true);
    define(std::string(var_name), elem_type, false);
    auto key_expr = lower_expr(map_of(node.get(la::KEY.code)));
    auto val_expr = lower_expr(map_of(node.get(la::VALUE.code)));
    lir::LExprPtr guard_body = nullptr;
    if (node.has_key(la::GUARD))
        guard_body = lower_expr(map_of(node.get(la::GUARD.code)));
    pop_scope();

    // Require KEY to be str (&[u8] slice).  Short-circuit on Error to avoid
    // cascading diagnostics when the key subexpression already failed.
    TypeRef kt = expr_type(key_expr);
    if (kt && TypeRef(kt).kind() == LogosType::Kind::Error)
        return error_expr();
    if (!(kt && TypeRef(kt).kind() == LogosType::Kind::Slice && TypeRef(kt).elem()
              && TypeRef(kt).elem().kind() == LogosType::Kind::U8)) {
        error(std::format(
            "writ map comprehension: key expression must be str (got {})",
            type_str(kt)));
        return error_expr();
    }

    // Coerce VALUE to AnyVal (no-op if already AnyVal).
    val_expr = coerce_to_writ_anyval(
        std::move(val_expr), ctr_var, ctr_t,
        "writ map comprehension value");
    if (!val_expr || TypeRef(expr_type(val_expr)).kind() == LogosType::Kind::Error)
        return error_expr();

    // Guard must be Bool; reject anything else early to avoid MLIR crashes
    // (cf.cond_br requires i1) and to silence cascades when the guard errored.
    if (guard_body) {
        auto gk = expr_type(guard_body) ? TypeRef(expr_type(guard_body)).kind()
                                   : LogosType::Kind::Error;
        if (gk == LogosType::Kind::Error)
            return error_expr();
        if (gk != LogosType::Kind::Bool) {
            error(std::format(
                "writ map comprehension: guard must be bool (got {})",
                type_str(expr_type(guard_body))));
            return error_expr();
        }
    }

    std::string new_sym = new_fi->symbol_name.empty() ? "writ_map_comp_new"
                                                      : new_fi->symbol_name;
    // Byte-cap hint for zone, and slot-count hint for map buckets.
    // For slices (arr_size==0 at compile time) we don't know iter length, so
    // use a generous default to reduce the risk of silent drops.  This is a
    // v1 limitation — objectmap_set has no auto-grow.
    int64_t slot_hint = arr_size > 0 ? arr_size : 64;
    int64_t cap_hint  = arr_size > 0 ? (arr_size * 48 + 256) : 4096;
    std::vector<lir::LExprPtr> new_args;
    new_args.push_back(builder().lit_int(cap_hint, prim(LogosType::Kind::I64)));
    new_args.push_back(builder().lit_int(slot_hint, prim(LogosType::Kind::I64)));
    auto call_new = builder().call(new_sym, {}, std::move(new_args), ctr_t);
    lir::SLet let_c;
    let_c.name   = ctr_var;
    let_c.type   = ctr_t;
    let_c.is_mut = true;
    let_c.value  = std::move(call_new);

    std::string put_sym = put_fi->symbol_name.empty() ? "writ_map_comp_put"
                                                      : put_fi->symbol_name;
    auto recv = builder().addr_of(ctr_var, make_ref(false, ctr_t));  // &Rc<Writ>
    std::vector<lir::LExprPtr> put_args;
    put_args.push_back(std::move(recv));
    put_args.push_back(std::move(key_expr));
    put_args.push_back(std::move(val_expr));
    auto put_call = builder().call(put_sym, {}, std::move(put_args), void_t());

    lir::SExprStmt put_stmt;
    put_stmt.expr = std::move(put_call);

    std::vector<lir_view::StmtRef> loop_body;
    if (guard_body) {
        lir::SIf sif;
        sif.cond = std::move(guard_body);
        std::vector<lir_view::StmtRef> then_blk;
        then_blk.push_back(make_stmt_emit(node_line_, std::move(put_stmt)));
        sif.then_ = lir_mirror_block(*cur_prog_, then_blk);
        loop_body.push_back(make_stmt_emit(node_line_, std::move(sif)));
    } else {
        loop_body.push_back(make_stmt_emit(node_line_, std::move(put_stmt)));
    }

    lir::SForEach sfe;
    sfe.var       = std::string(var_name);
    sfe.iter      = std::move(iter);
    sfe.elem_type = elem_type;
    sfe.arr_size  = arr_size;
    sfe.is_slice  = is_slice;
    sfe.body      = lir_mirror_block(*cur_prog_, loop_body);

    std::vector<lir_view::StmtRef> outer;
    outer.push_back(make_stmt_emit(node_line_, std::move(let_c)));
    outer.push_back(make_stmt_emit(node_line_, std::move(sfe)));

    auto result = builder().var_ref(ctr_var, ctr_t);
    return builder().block_expr(lir_mirror_block(*cur_prog_, outer), std::move(result), ctr_t);
}

// Coerce an arbitrary value to AnyVal for use inside a Writ comprehension.
// Returns the original expr if already AnyVal; otherwise wraps in a call to
// one of the `writ_coerce_*` helpers in writ/ctr.logos. String coercion
// requires `&mut ctr_var` because the string is copied into the zone.
lir::LExprPtr SemaChecker::coerce_to_writ_anyval(
        lir::LExprPtr val,
        const std::string& ctr_var,
        TypeRef ctr_t,
        std::string_view context) {
    if (!val || !expr_type(val)) return val;
    TypeRef t = expr_type(val);

    // Pass Error through unchanged — caller short-circuits on Error without
    // emitting an additional "cannot auto-coerce <error>" diagnostic.
    if (TypeRef(t).kind() == LogosType::Kind::Error) return val;

    // WAny passthrough (writ): an element already produced as an WAny value.
    if (TypeRef(t).kind() == LogosType::Kind::Enum
        && TypeRef(t).enum_name() == "WAny") {
        return val;
    }
    // AnyVal passthrough (legacy datatype/struct form) — legacy.
    if ((TypeRef(t).kind() == LogosType::Kind::Struct
         || TypeRef(t).kind() == LogosType::Kind::ZonedStruct)
        && is_anyval(t)) {
        return val;
    }

    const char* helper = nullptr;
    bool needs_ctr = false;
    using K = LogosType::Kind;
    switch (TypeRef(t).kind()) {
        case K::Bool: helper = "writ_coerce_bool"; break;
        case K::I8:   helper = "writ_coerce_i8";   break;
        case K::I16:  helper = "writ_coerce_i16";  break;
        case K::I32:  case K::IntLit:
                      helper = "writ_coerce_i32"; break;
        case K::U8:   helper = "writ_coerce_u8";   break;
        case K::U16:  helper = "writ_coerce_u16";  break;
        case K::U32:  helper = "writ_coerce_u32"; break;
        // i64/u64/i24/u24/i56/u56/i128/u128 intentionally omitted: embedding
        // them via i32 would silently truncate high bits.  User must cast
        // explicitly (e.g. `x as i32`) or wrap with WAny::from.
        case K::Slice:
            if (TypeRef(t).elem() && TypeRef(t).elem().kind() == K::U8) {
                helper = "writ_coerce_str";
                needs_ctr = true;
            }
            break;
        default: break;
    }

    if (!helper) {
        error(std::format(
            "{}: cannot auto-coerce {} to AnyVal; cast to i32/u32/bool/str "
            "explicitly, or wrap with AnyVal::embed_*",
            context, type_str(t)));
        return error_expr();
    }

    size_t want_arity = needs_ctr ? 2 : 1;
    auto cands = find_func_candidates(helper);
    const SemaFuncInfo* fi = nullptr;
    for (auto* c : cands) {
        if (c->param_types.size() == want_arity) { fi = c; break; }
    }
    if (!fi) {
        error(std::format("{}: {} not found; `use logos.mem.writ.ctr;`",
                          context, helper));
        return error_expr();
    }
    TypeRef ret_t = fi->ret_type;

    std::vector<lir::LExprPtr> args;
    if (needs_ctr) {
        auto recv = builder().addr_of(ctr_var, make_ref(false, ctr_t));  // &Rc<Writ>
        args.push_back(std::move(recv));
    }
    args.push_back(std::move(val));
    std::string sym = fi->symbol_name.empty() ? helper : fi->symbol_name;
    return builder().call(sym, {}, std::move(args), ret_t);
}

lir::LExprPtr SemaChecker::lower_arr_fill_lit(TinyMapView node) {
    auto val_node = map_of(node.get(la::VALUE.code));
    auto fill_val = lower_expr(val_node);
    TypeRef elem_type = expr_type(fill_val);
    // ONE resolver, shared with the type position — which is what makes
    // `[v; K]` with a const-generic K work at last: the expression position
    // used to accept only module consts, so `[u64; K]` compiled as a type and
    // failed as an expression.
    auto len = resolve_array_len(node.has_key(la::SIZE)
                                  ? map_of(node.get(la::SIZE.code))
                                  : writ::TinyMapView{});
    if (!len.ok) return error_expr();
    if (!len.symbolic.empty()) {
        // A length that is not known yet (a symbolic name, or a deferred const
        // EXPRESSION postfix-encoded in `symbolic`): emit ONE element carrying
        // the unresolved size; mono repeats it once the length folds.
        LogosTypeBuilder ab;
        ab.kind = LogosType::Kind::Array;
        ab.elem = elem_type;
        ab.arr_size = 0;
        ab.arr_size_var = len.symbolic;
        TypeRef arr_t = pool_->alloc(std::move(ab));
        std::vector<lir::LExprPtr> one;
        one.push_back(std::move(fill_val));
        return builder().arr_lit(std::move(one), arr_t);
    }
    int64_t n = static_cast<int64_t>(len.value);
    // Keep IntLit unresolved so that struct-literal type inference (hint_struct_type_)
    // can widen the element to the correct concrete type (e.g. i64 for Vec<i64>).
    std::vector<lir::LExprPtr> elems;
    elems.push_back(std::move(fill_val));
    for (int64_t i = 1; i < n; ++i)
        elems.push_back(lower_expr(val_node));  // re-lower for each slot (simple literals)
    return builder().arr_lit(std::move(elems), make_array(elem_type, (size_t)n));
}

// g9/B121: generic associated-const projection `T::CONST`. When T (`cname`) is
// an abstract type-param bounded by a trait that declares `const CONST`
// (`mname`), the concrete value is known only after T is substituted at mono.
// Route the read through a zero-arg accessor call `T__kassoc_CONST()`: mono
// rewrites the `T__` prefix to the concrete type (its existing generic-static
// dispatch ECall rewrite) and codegen calls the per-impl accessor LFunction
// `Concrete__kassoc_CONST` emitted in lower_impl_block. Mirrors the generic-
// static-method dispatch path. Returns nullptr if `cname` is not such a
// projection (caller falls through to its normal "unknown enum" handling).
lir::LExprPtr SemaChecker::try_lower_generic_assoc_const(const std::string& cname,
                                                         const std::string& mname) {
    auto bit = current_type_bounds_.find(cname);
    if (bit == current_type_bounds_.end()) return nullptr;
    // Transitively close the bounds over supertraits (the const may be declared
    // on a supertrait of a stated bound), then find a trait declaring `mname`.
    logos::compiler::StrSet seen;
    std::vector<std::string> search_traits;
    std::function<void(const std::string&)> add_t =
        [&](const std::string& tn) {
            if (!seen.insert(tn).second) return;
            search_traits.push_back(tn);
            auto it = find_trait_iter_scoped(tn);
            if (it != traits_.end())
                for (auto& s : it->second.supertraits) add_t(s.trait_name);
        };
    for (auto& b : bit->second) add_t(b.trait_name);
    for (auto& tn : search_traits) {
        auto tit = find_trait_iter_scoped(tn);
        if (tit == traits_.end()) continue;
        for (auto& ac : tit->second.assoc_consts) {
            if (ac.name != mname) continue;
            TypeRef ret_t = ac.type ? ac.type : prim(LogosType::Kind::I64);
            return builder().call(cname + "__kassoc_" + mname, {}, {}, ret_t);
        }
    }
    return nullptr;
}

lir::LExprPtr SemaChecker::lower_enum_lit(TinyMapView node) {
    std::string ename_buf(str_of(node.get(la::NAME.code)));
    // T2-28 (Increment 2): a fully-qualified no-paren path `pkg.path.Type::member`
    // (unit enum variant or associated const) arrives with NAME = first segment
    // + QUAL_PARTS = the rest; the LAST segment is the type. (FIELD already holds
    // the member.) The package prefix is dropped — lookup is by type name.
    if (node.has_key(la::QUAL_PARTS)) {
        auto parts = arr_of(node.get(la::QUAL_PARTS.code));
        if (parts.size() >= 1)
            ename_buf = std::string(
                str_of(map_of(parts.get(parts.size() - 1)).get(la::NAME.code)));
    }
    // G160-1: `Self::Qux` (unit variant) inside an `impl Enum` body — resolve
    // `Self` to the enclosing enum's name so the variant lookup succeeds (the
    // tuple-variant form `Self::Bar(x)` already resolves via lower_static_call;
    // the struct-shaped form is handled in the struct-lit path).
    TypeRef selfveu_self_ = nullptr;  // PROBE selfveu
    if (ename_buf == "Self") {
        auto sit = current_type_params_.find("Self");
        if (sit != current_type_params_.end() && sit->second &&
            TypeRef(sit->second).kind() == LogosType::Kind::Enum) {
            ename_buf = std::string(TypeRef(sit->second).enum_name());
            if (logos::probe::on("selfveu")) selfveu_self_ = sit->second;  // PROBE selfveu
        }
    }
    // G160-2: peel a non-generic type-alias to an enum (`type A = Foo; A::Qux`).
    if (!enums_.count(ename_buf) && !find_enum_by_name(ename_buf).second) {
        auto ait = alias_find(ename_buf);
        if (ait != type_aliases_.end() && ait->second.type_params.empty() &&
            ait->second.type &&
            TypeRef(ait->second.type).kind() == LogosType::Kind::Enum)
            ename_buf = std::string(TypeRef(ait->second.type).enum_name());
    }
    std::string_view ename = ename_buf;
    auto vname = str_of(node.get(la::FIELD.code));
    auto [epkg_el, esi_el] = find_enum_by_name(ename);
    auto eit = esi_el ? enums_.find(sema_key(epkg_el, std::string(ename))) : enums_.end();
    if (eit == enums_.end()) eit = enums_.find(std::string(ename));
    if (eit == enums_.end()) {
        // Before reporting "unknown enum", check if this is an associated constant
        // access (e.g. Buffer::MAX) parsed as ENUM_LIT due to grammar ambiguity.
        std::string cname_str = std::string(ename);
        std::string mname_str = std::string(vname);
        // B97: try inherent assoc-const first (`impl S { const C: T = ... }`).
        {
            std::string key = "inherent::" + cname_str + "::" + mname_str;
            auto cit = assoc_const_impls_.find(key);
            if (cit != assoc_const_impls_.end()) {
                if (!cit->second.cached_value) {
                    auto val = lower_expr(map_of(cit->second.value_ast));
                    if (cit->second.type) builder().retype_expr(val, cit->second.type);
                    cit->second.cached_value = val;
                }
                return cit->second.cached_value;
            }
        }
        for (auto& [tname, tinfo] : traits_) {
            if (!impls_.count(tname + "::" + cname_str)) continue;
            std::string key = tname + "::" + cname_str + "::" + mname_str;
            auto cit = assoc_const_impls_.find(key);
            if (cit != assoc_const_impls_.end()) {
                if (!cit->second.cached_value) {
                    auto val = lower_expr(map_of(cit->second.value_ast));
                    if (cit->second.type) builder().retype_expr(val, cit->second.type);
                    cit->second.cached_value = val;
                }
                return cit->second.cached_value;
            }
        }
        // g9/B121: generic assoc-const projection `T::CONST` (T a bound
        // type-param) — route through a per-impl accessor call.
        if (auto acc = try_lower_generic_assoc_const(cname_str, mname_str))
            return acc;
        // Method as a fn-pointer value: `let f = A::bar;` — an INHERENT method
        // path in value position (not a call). Resolve `A__bar` and emit a FnPtr
        // var-ref over its signature, mirroring the bare-fn-name path in
        // lower_var_ref. The call site `f(&a)` then dispatches as a fn-ptr call.
        {
            std::string msym = cname_str + "__" + mname_str;
            const SemaFuncInfo* mfi = nullptr;
            auto mcands = find_func_candidates(msym);
            if (mcands.size() == 1) mfi = mcands[0];
            // G172-13: trait-qualified form `Foo::foo` (Foo a trait, not a
            // type). Rust infers Self from the call site; we resolve it only when
            // there's exactly ONE impl of the trait in scope (unambiguous — Rust
            // also errors on a trait method fn-ptr with multiple candidate Selfs
            // and no annotation). The unique impl's `<Concrete>__foo` supplies
            // the signature.
            if (!mfi && find_trait_by_name(cname_str).second) {
                std::string prefix = cname_str + "::";
                std::string sole_concrete;
                int n_impls = 0;
                for (auto& [k, _v] : impls_) {
                    if (k.size() <= prefix.size() ||
                        k.compare(0, prefix.size(), prefix) != 0) continue;
                    std::string concrete = k.substr(prefix.size());
                    // Skip trait-arg-keyed coherence entries (contain '[').
                    if (concrete.find('[') != std::string::npos) continue;
                    if (concrete != sole_concrete) { ++n_impls; sole_concrete = concrete; }
                }
                if (n_impls == 1) {
                    auto cc = find_func_candidates(sole_concrete + "__" + mname_str);
                    if (cc.size() == 1) { mfi = cc[0]; msym = sole_concrete + "__" + mname_str; }
                }
            }
            if (mfi && mfi->type_params.empty()) {
                LogosTypeBuilder ft;
                ft.kind = LogosType::Kind::FnPtr;
                for (auto pt : mfi->param_types) ft.closure_params.push_back(pt);
                ft.closure_ret = mfi->ret_type ? mfi->ret_type : void_t();
                auto fn_type = pool_->alloc(std::move(ft));
                return builder().var_ref(
                    mfi->symbol_name.empty() ? msym : mfi->symbol_name, fn_type);
            }
        }
        error(std::format("unknown enum '{}'", ename));
        return error_expr();
    }
    int64_t disc = 0;
    bool found = false;
    for (auto& v : eit->second.variants)
        if (v.name == vname) { disc = v.value; found = true; break; }
    if (!found) {
        // B97: enum exists but variant not found — could be assoc const
        // on an enum (rare). Try inherent lookup as last resort.
        std::string key = "inherent::" + std::string(ename) + "::" + std::string(vname);
        auto cit = assoc_const_impls_.find(key);
        if (cit != assoc_const_impls_.end()) {
            if (!cit->second.cached_value) {
                auto val = lower_expr(map_of(cit->second.value_ast));
                if (cit->second.type) builder().retype_expr(val, cit->second.type);
                cit->second.cached_value = val;
            }
            return cit->second.cached_value;
        }
        error(std::format("enum '{}' has no variant '{}'", ename, vname));
        return error_expr();
    }
    // SL-sl-03: a payload-less variant (`Option::None`) on a generic enum
    // has no inference source for its type-args. Consult `hint_enum_type_`
    // (set by the call-site / let / deref-write context) so the resulting
    // type is `Option<i32>` instead of bare `Option` — mlir-gen needs the
    // concrete name to find the tagged-enum layout.
    TypeRef result_t = make_enum_type(ename, epkg_el);
    auto& einfo_for_hint = eit->second;
    if (!einfo_for_hint.type_params.empty() &&
        hint_enum_type_ &&
        TypeRef(hint_enum_type_).kind() == LogosType::Kind::Enum &&
        TypeRef(hint_enum_type_).enum_name() == std::string(ename) &&
        TypeRef(hint_enum_type_).type_args().size() == einfo_for_hint.type_params.size()) {
        std::vector<TypeRef> targs;
        for (auto a : TypeRef(hint_enum_type_).type_args()) targs.push_back(a);
        std::vector<std::string> lt_args;
        for (auto& lp : einfo_for_hint.lifetime_params) {
            (void)lp;
            lt_args.push_back(std::string{});
        }
        result_t = make_generic_enum(std::string(ename), std::move(targs), std::move(lt_args));
    }
    // PROBE selfveu — src/compiler/PROBES.md 2026-09-13a: a unit variant spelled `Self::V` has no
    // values to type it; it IS the impl self type, regions included.
    if (logos::probe::on("selfveu") && selfveu_self_ &&
        TypeRef(selfveu_self_).enum_name() == std::string(ename) &&
        TypeRef(selfveu_self_).type_args().empty() &&
        !TypeRef(selfveu_self_).lifetime_args().empty()) {
        logos::probe::census("selfveu.retyped");
        result_t = selfveu_self_;
    }
    return builder().enum_lit(std::string(ename), std::string(vname), disc, result_t);
}

lir::LExprPtr SemaChecker::lower_enum_lit_data(TinyMapView node) {
    std::string ename_buf(str_of(node.get(la::NAME.code)));
    // G160-1: `Self::Baz { .. }` / `Self::Bar(x)` inside an `impl Enum` body —
    // resolve `Self` to the enclosing enum name (mirrors lower_enum_lit).
    if (ename_buf == "Self") {
        auto sit = current_type_params_.find("Self");
        if (sit != current_type_params_.end() && sit->second &&
            TypeRef(sit->second).kind() == LogosType::Kind::Enum)
            ename_buf = std::string(TypeRef(sit->second).enum_name());
    }
    // G160-2: peel a non-generic type-alias to an enum.
    if (!enums_.count(ename_buf) && !find_enum_by_name(ename_buf).second) {
        auto ait = alias_find(ename_buf);
        if (ait != type_aliases_.end() && ait->second.type_params.empty() &&
            ait->second.type &&
            TypeRef(ait->second.type).kind() == LogosType::Kind::Enum)
            ename_buf = std::string(TypeRef(ait->second.type).enum_name());
    }
    std::string_view ename = ename_buf;
    auto vname = str_of(node.get(la::FIELD.code));
    auto [epkg_eld, esi_eld] = find_enum_by_name(ename);
    auto eit = esi_eld ? enums_.find(sema_key(epkg_eld, std::string(ename))) : enums_.end();
    if (eit == enums_.end()) eit = enums_.find(std::string(ename));
    if (eit == enums_.end()) {
        // Bug 5 fix: ENUM_LIT_DATA shares the same grammar path as ENUM_LIT.
        // Check for associated constant access before reporting "unknown enum".
        std::string cname_str = std::string(ename);
        std::string mname_str = std::string(vname);
        for (auto& [tname, tinfo] : traits_) {
            if (!impls_.count(tname + "::" + cname_str)) continue;
            std::string key = tname + "::" + cname_str + "::" + mname_str;
            auto cit = assoc_const_impls_.find(key);
            if (cit != assoc_const_impls_.end()) {
                if (!cit->second.cached_value) {
                    auto val = lower_expr(map_of(cit->second.value_ast));
                    if (cit->second.type) builder().retype_expr(val, cit->second.type);
                    cit->second.cached_value = val;
                }
                return cit->second.cached_value;
            }
        }
        // g9/B121: generic assoc-const projection `T::CONST` (T a bound
        // type-param) — route through a per-impl accessor call.
        if (auto acc = try_lower_generic_assoc_const(cname_str, mname_str))
            return acc;
        // Method as a fn-pointer value: `let f = A::bar;` — an INHERENT method
        // path in value position (not a call). Resolve `A__bar` and emit a FnPtr
        // var-ref over its signature, mirroring the bare-fn-name path in
        // lower_var_ref. The call site `f(&a)` then dispatches as a fn-ptr call.
        {
            std::string msym = cname_str + "__" + mname_str;
            const SemaFuncInfo* mfi = nullptr;
            auto mcands = find_func_candidates(msym);
            if (mcands.size() == 1) mfi = mcands[0];
            // G172-13: trait-qualified form `Foo::foo` (Foo a trait, not a
            // type). Rust infers Self from the call site; we resolve it only when
            // there's exactly ONE impl of the trait in scope (unambiguous — Rust
            // also errors on a trait method fn-ptr with multiple candidate Selfs
            // and no annotation). The unique impl's `<Concrete>__foo` supplies
            // the signature.
            if (!mfi && find_trait_by_name(cname_str).second) {
                std::string prefix = cname_str + "::";
                std::string sole_concrete;
                int n_impls = 0;
                for (auto& [k, _v] : impls_) {
                    if (k.size() <= prefix.size() ||
                        k.compare(0, prefix.size(), prefix) != 0) continue;
                    std::string concrete = k.substr(prefix.size());
                    // Skip trait-arg-keyed coherence entries (contain '[').
                    if (concrete.find('[') != std::string::npos) continue;
                    if (concrete != sole_concrete) { ++n_impls; sole_concrete = concrete; }
                }
                if (n_impls == 1) {
                    auto cc = find_func_candidates(sole_concrete + "__" + mname_str);
                    if (cc.size() == 1) { mfi = cc[0]; msym = sole_concrete + "__" + mname_str; }
                }
            }
            if (mfi && mfi->type_params.empty()) {
                LogosTypeBuilder ft;
                ft.kind = LogosType::Kind::FnPtr;
                for (auto pt : mfi->param_types) ft.closure_params.push_back(pt);
                ft.closure_ret = mfi->ret_type ? mfi->ret_type : void_t();
                auto fn_type = pool_->alloc(std::move(ft));
                return builder().var_ref(
                    mfi->symbol_name.empty() ? msym : mfi->symbol_name, fn_type);
            }
        }
        // `Z::method::<T..>(args)` — Z a type-param bound by a trait declaring a
        // static `method`. The grammar parses this identically to a generic
        // enum-variant construction; disambiguate when NAME is a bound type-param.
        if (current_type_bounds_.count(cname_str)) {
            std::vector<lir::LExprPtr> sargs;
            if (node.has_key(la::ARGS)) {
                AnyVal aav = node.get(la::ARGS.code);
                auto run = [&](auto items) {
                    for (uint64_t i = 0; i < items.size(); ++i)
                        sargs.push_back(lower_expr(map_of(items.get(i))));
                };
                if (!aav.is_null()) {
                    if (aav.is_pointer()) {
                        auto mm = map_of(aav);
                        if (mm.has_key(la::ITEMS)) run(arr_of(mm.get(la::ITEMS.code)));
                        else                       run(arr_of(aav));
                    } else run(arr_of(aav));
                }
            }
            if (auto c = lower_typaram_static_method(cname_str, mname_str,
                            collect_type_args(node), std::move(sargs)))
                return c;
        }
        error(std::format("unknown enum '{}'", ename));
        return error_expr();
    }
    const SemaVariantInfo* vinfo = nullptr;
    for (auto& v : eit->second.variants)
        if (v.name == vname) { vinfo = &v; break; }
    if (!vinfo) {
        error(std::format("enum '{}' has no variant '{}'", ename, vname));
        return error_expr();
    }

    // Lower payload arguments. ARGS is either a direct array (legacy
    // `enum_lit` alt with `$...`) or a { ITEMS: [...] } map (turbofish
    // alt routes through enum_lit_args sub-production). Accept both.
    std::vector<lir::LExprPtr> payload;
    bool is_struct_shape_lit = node.has_key(la::variant::IS_STRUCT_SHAPE) &&
        node.get(la::variant::IS_STRUCT_SHAPE.code).as_value<int32_t>() != 0;
    if (is_struct_shape_lit) {
        // P4-pm-01: `E::V { name: expr, ... }` — items list of
        // FIELD_INIT / FIELD_SHORTHAND. Resolve names → variant
        // payload positions via the variant's payload_field_names,
        // produce positional `payload` in declaration order.
        // An empty struct-shape variant (`E::Empty {}`) legitimately has no
        // fields — gate on the declared shape, not on field-count, so the
        // empty case is accepted (produces an empty payload).
        if (!vinfo->is_struct_shape && vinfo->payload_field_names.empty()) {
            error(std::format(
                "{}::{} is not a struct-shape variant — use `{}::{}({{args...}})` or no payload",
                ename, vname, ename, vname));
        }
        size_t arity = vinfo->payload_field_names.size();
        std::vector<lir::LExprPtr> by_pos(arity);
        std::vector<bool> seen(arity, false);
        std::vector<std::string> provided;
        if (node.has_key(la::ITEMS)) {
            auto items_av = node.get(la::ITEMS.code);
            ArrayView fitems;
            if (!items_av.is_null()) {
                if (items_av.is_pointer()) {
                    auto m = map_of(items_av);
                    if (m.has_key(la::ITEMS)) fitems = arr_of(m.get(la::ITEMS.code));
                    else                       fitems = arr_of(items_av);
                } else {
                    fitems = arr_of(items_av);
                }
            }
            for (uint64_t i = 0; i < fitems.size(); ++i) {
                auto fnode = map_of(fitems.get(i));
                std::string fname;
                if (fnode.has_key(la::NAME))
                    fname = std::string(str_of(fnode.get(la::NAME.code)));
                // Locate field index in variant declaration order.
                size_t idx = arity;
                for (size_t k = 0; k < arity; ++k)
                    if (vinfo->payload_field_names[k] == fname) { idx = k; break; }
                if (idx == arity) {
                    error(std::format("{}::{}: no field named '{}'",
                          ename, vname, fname));
                    continue;
                }
                if (seen[idx]) {
                    error(std::format("{}::{}: field '{}' specified more than once",
                          ename, vname, fname));
                    continue;
                }
                // FIELD_INIT carries VALUE; FIELD_SHORTHAND is `name` only —
                // synthesise an EVarRef of the same name.
                // The variant field's declared payload type — a CONCRETE enum
                // hints a payload-less enum-literal value (`E::V { f: None }`)
                // so it heap-allocates with the right monomorphisation instead
                // of lowering as a bare C-style enum the heap-ptr slot mis-reads.
                TypeRef fld_decl_ty = (idx < vinfo->payload_types.size())
                    ? vinfo->payload_types[idx] : TypeRef(nullptr);
                if (fld_decl_ty && !eit->second.type_params.empty() && hint_enum_type_ &&
                    TypeRef(hint_enum_type_).enum_name() == ename) {
                    SemaSubst fs;
                    auto hta = TypeRef(hint_enum_type_).type_args();
                    for (size_t k = 0; k < eit->second.type_params.size() && k < hta.size(); ++k)
                        if (hta[k]) fs[eit->second.type_params[k].name] = hta[k];
                    if (!fs.empty()) fld_decl_ty = subst_type_sema(fld_decl_ty, fs);
                }
                bool fld_concrete_enum = fld_decl_ty &&
                    TypeRef(fld_decl_ty).kind() == LogosType::Kind::Enum &&
                    !TypeRef(fld_decl_ty).type_args().empty() &&
                    !enum_arg_unresolved(fld_decl_ty);
                lir::LExprPtr val = nullptr;
                int32_t fcode = code_of(fnode);
                if (fnode.has_key(la::VALUE)) {
                    TypeRef saved_eh = hint_enum_type_;
                    if (fld_concrete_enum) hint_enum_type_ = fld_decl_ty;
                    val = lower_expr(map_of(fnode.get(la::VALUE.code)));
                    hint_enum_type_ = saved_eh;
                    if (fld_concrete_enum) try_retype_bare_enum_arg(val, fld_decl_ty);
                } else if (fcode == la::FIELD_SHORTHAND) {
                    TypeRef rt = lookup(fname);
                    if (!rt) {
                        error(std::format("{}::{}: shorthand '{}' — name not in scope",
                              ename, vname, fname));
                        val = error_expr();
                    } else {
                        val = builder().var_ref(fname, rt);
                    }
                } else {
                    error(std::format("{}::{}: internal — field-init missing VALUE", ename, vname));
                    val = error_expr();
                }
                by_pos[idx] = std::move(val);
                seen[idx] = true;
                provided.push_back(fname);
            }
        }
        (void)provided;
        // Missing-field diagnostics — report all in one shot.
        std::vector<std::string> missing;
        for (size_t k = 0; k < arity; ++k)
            if (!seen[k])
                missing.push_back(vinfo->payload_field_names[k]);
        if (!missing.empty()) {
            std::string list;
            for (size_t k = 0; k < missing.size(); ++k) {
                if (k) list += ", ";
                list += "'" + missing[k] + "'";
            }
            error(std::format("{}::{}: missing field(s): {}", ename, vname, list));
        }
        for (auto& p : by_pos) {
            if (!p) p = error_expr();
            payload.push_back(std::move(p));
        }
    } else if (node.has_key(la::ARGS)) {
        AnyVal args_av = node.get(la::ARGS.code);
        // K4-root: project the outer hint's type-args through this variant's
        // payload TypeVars so a nested payload-less enum literal
        // (`Option::Some(Option::None)` with hint `Option<Option<i64>>`) gets
        // its OWN concrete hint (`Option<i64>`) and heap-allocates with the
        // right monomorphisation — otherwise the inner `None` stays a bare
        // C-style enum and the outer slot (a heap-ptr) reads garbage at deref.
        // Mirrors lower_enum_lit_data_from_static's pre_subst narrowing.
        SemaSubst pre_subst;
        if (hint_enum_type_ &&
            TypeRef(hint_enum_type_).kind() == LogosType::Kind::Enum &&
            TypeRef(hint_enum_type_).enum_name() == std::string(ename) &&
            !eit->second.type_params.empty()) {
            auto hta = TypeRef(hint_enum_type_).type_args();
            for (size_t k = 0; k < eit->second.type_params.size() && k < hta.size(); ++k) {
                if (!hta[k] || TypeRef(hta[k]).kind() == LogosType::Kind::Error) continue;
                pre_subst[eit->second.type_params[k].name] = hta[k];
            }
        }
        auto run = [&](auto items) {
            for (uint64_t i = 0; i < items.size(); ++i) {
                TypeRef saved_hint = hint_enum_type_;
                if (i < vinfo->payload_types.size()) {
                    TypeRef pt_i = vinfo->payload_types[i];
                    if (pt_i && !pre_subst.empty())
                        pt_i = subst_type_sema(pt_i, pre_subst);
                    if (pt_i && TypeRef(pt_i).kind() == LogosType::Kind::Enum)
                        hint_enum_type_ = pt_i;
                }
                auto e = lower_expr(map_of(items.get(i)));
                hint_enum_type_ = saved_hint;
                if (TypeRef(expr_type(e)).kind() == LogosType::Kind::Void) continue;
                payload.push_back(std::move(e));
            }
        };
        if (!args_av.is_null()) {
            if (args_av.is_pointer()) {
                auto m = map_of(args_av);
                if (m.has_key(la::ITEMS)) run(arr_of(m.get(la::ITEMS.code)));
                else                       run(arr_of(args_av));
            } else {
                run(arr_of(args_av));
            }
        }
    }

    // Resolve payload types — substitute TypeVars if generic enum
    auto& einfo = eit->second;
    std::vector<TypeRef> resolved_payload_types = vinfo->payload_types;

    // B81: infer enum's lifetime args from paired payload types. Walk each
    // (declared payload type, actual value type) pair extracting lifetimes
    // from Refs and mapping back to einfo.lifetime_params. Used to populate
    // lifetime_args on the constructed enum type so the variance check at
    // return-site / let-init can compare lifetimes.
    std::unordered_map<std::string, std::string> lt_subst;
    LtCands lt_cands;
    {
        std::function<void(TypeRef, TypeRef)> walk = [&](TypeRef pt, TypeRef at) {
            if (!pt || !at) return;
            using K = LogosType::Kind;
            auto pk = pt.kind();
            if ((pk == K::Ref || pk == K::MutRef) && (at.kind() == K::Ref || at.kind() == K::MutRef)) {
                std::string pl(pt.lifetime()), al(at.lifetime());
                if (!pl.empty() && !al.empty()) {
                    lt_cands[pl].push_back(al);
                    if (!lt_subst.count(pl)) lt_subst[pl] = al;
                }
                walk(pt.pointee(), at.pointee());
                return;
            }
            if (pk == K::Tuple && at.kind() == K::Tuple) {
                auto pe = pt.tuple_elems(); auto ae = at.tuple_elems();
                for (size_t i = 0; i < pe.size() && i < ae.size(); ++i) walk(pe[i], ae[i]);
                return;
            }
            if ((pk == K::Struct || pk == K::ZonedStruct || pk == K::Enum) && at.kind() == pk) {
                auto pl = pt.lifetime_args(); auto al = at.lifetime_args();
                for (size_t i = 0; i < pl.size() && i < al.size(); ++i) {
                    if (!pl[i].empty() && !al[i].empty() && !lt_subst.count(pl[i]))
                        lt_subst[pl[i]] = al[i];
                }
                auto pa = pt.type_args(); auto aa = at.type_args();
                for (size_t i = 0; i < pa.size() && i < aa.size(); ++i) walk(pa[i], aa[i]);
                return;
            }
        };
        for (size_t i = 0; i < vinfo->payload_types.size() && i < payload.size(); ++i)
            if (payload[i]) walk(vinfo->payload_types[i], expr_type(payload[i]));
        census_meet_("enumlit", einfo.lifetime_params, lt_cands, eit->first);
    }

    // Build the enum type (may be generic, e.g. Option<i32>)
    // For now, if the enum has type params, we need to infer them from payload types.
    // Simple inference: match payload args to payload type params.
    TypeRef result_type = make_enum_type(ename);
    if (!einfo.type_params.empty()) {
        // Build substitution from payload args. When a payload is an unresolved
        // integer/float literal AND the let/return hint pins that type-param to
        // a concrete type (`let a: MyOpt<i64> = MyOpt::MSome(3)`), prefer the
        // hint over the i32/f64 literal default — otherwise `3` would default
        // the enum's T to i32 and the annotation's i64 would be ignored, so
        // a downstream `impl<T> ... for MyOpt<T>` method instantiates at the
        // wrong T (root cause of G150-2's generic-enum-`==` blocker).
        bool hint_matches = hint_enum_type_ &&
            TypeRef(hint_enum_type_).enum_name() == std::string(ename) &&
            !TypeRef(hint_enum_type_).type_args().empty();
        auto hint_for_param = [&](std::string_view tvname) -> TypeRef {
            if (!hint_matches) return nullptr;
            for (size_t k = 0; k < einfo.type_params.size() &&
                               k < TypeRef(hint_enum_type_).type_args().size(); ++k)
                if (einfo.type_params[k].name == tvname) {
                    auto h = TypeRef(hint_enum_type_).type_args()[k];
                    if (h && TypeRef(h).kind() != LogosType::Kind::Error) return h;
                    break;
                }
            return nullptr;
        };
        SemaSubst subst;
        for (size_t i = 0; i < vinfo->payload_types.size() && i < payload.size(); ++i) {
            auto pt = vinfo->payload_types[i];
            if (pt && TypeRef(pt).kind() == LogosType::Kind::TypeVar) {
                auto inferred = expr_type(payload[i]);
                std::string tvn(TypeRef(pt).type_var_name());
                if (TypeRef(inferred).kind() == LogosType::Kind::IntLit ||
                    TypeRef(inferred).kind() == LogosType::Kind::FloatLit) {
                    if (auto h = hint_for_param(tvn)) {
                        inferred = h;                  // annotation wins
                        widen_int_expr(payload[i], h, builder());  // pin the literal too
                    } else {
                        inferred = TypeRef(inferred).kind() == LogosType::Kind::FloatLit
                                   ? prim(LogosType::Kind::F64) : i32_t();
                    }
                }
                // G168-A: when the hint pins this type-param to a trait object
                // (`Option<Box<dyn Sh>>`) but the arg is a CONCRETE coercible
                // value (`Box<Sq>`), record the enum's type-arg as the DYN type
                // — so the constructed enum is genuinely `Option<Box<dyn Sh>>` —
                // while leaving the payload expr concrete. mlir-gen's
                // enum-payload store then unsize-fattens the concrete payload
                // into the dyn slot; otherwise a thin `Box<Sq>` is stored and
                // dispatch reads a garbage vtable (SIGSEGV).
                auto wraps_dyn = [&](TypeRef t) -> bool {
                    if (!t) return false;
                    TypeRef u(t);
                    if ((u.kind() == LogosType::Kind::Ref ||
                         u.kind() == LogosType::Kind::MutRef) && u.pointee())
                        u = u.pointee();
                    if (is_stdlib_box(u) && u.type_args().size() == 1)
                        u = u.type_args()[0];
                    return u.kind() == LogosType::Kind::TraitObject;
                };
                if (auto h = hint_for_param(tvn))
                    if (wraps_dyn(h) && !wraps_dyn(inferred) &&
                        types_compatible(inferred, h))
                        inferred = h;
                subst[tvn] = inferred;
            } else if (pt) {
                // N8: the declared payload type is a STRUCTURAL type that
                // mentions the enum's type params (e.g. `Full(Pair<T>)`), not a
                // bare TypeVar. Unify it against the actual arg type to extract
                // the nested bindings (`Pair<T>` vs `Pair<i64>` → T=i64) so a
                // turbofish/annotation isn't required for inference.
                unify_types(pt, expr_type(payload[i]), subst);
            }
        }
        // Fill any still-unresolved type params from hint (e.g. let e: Result<i32,i32> = Result::Err(-1))
        if (hint_enum_type_ && TypeRef(hint_enum_type_).enum_name() == std::string(ename)) {
            for (size_t i = 0; i < einfo.type_params.size() && i < TypeRef(hint_enum_type_).type_args().size(); ++i) {
                if (subst.find(einfo.type_params[i].name) == subst.end()) {
                    auto hta = TypeRef(hint_enum_type_).type_args()[i];
                    if (hta && TypeRef(hta).kind() != LogosType::Kind::Error)
                        subst[einfo.type_params[i].name] = hta;
                }
            }
        }
        // Build concrete type args
        std::vector<TypeRef> type_args;
        for (auto& tp : einfo.type_params) {
            auto sit = subst.find(tp.name);
            type_args.push_back(sit != subst.end() ? sit->second : error_t());
        }
        check_type_bounds(std::string(ename), einfo.type_params, type_args);
        // B81: emit lifetime_args from lt_subst (enum's lifetime_params
        // → inferred caller lifetimes). Use empty string for unresolved.
        std::vector<std::string> lt_args;
        for (auto& lp : einfo.lifetime_params) {
            auto it = lt_subst.find(lp);
            lt_args.push_back(it != lt_subst.end() ? it->second : std::string{});
        }
        result_type = make_generic_enum(ename, std::move(type_args), std::move(lt_args));
        // Resolve payload types with substitution
        for (size_t i = 0; i < resolved_payload_types.size(); ++i)
            resolved_payload_types[i] = subst_type_sema(resolved_payload_types[i], subst);
    } else if (!einfo.lifetime_params.empty()) {
        // Non-generic enum but has lifetime params — emit lt_args still.
        std::vector<std::string> lt_args;
        for (auto& lp : einfo.lifetime_params) {
            auto it = lt_subst.find(lp);
            lt_args.push_back(it != lt_subst.end() ? it->second : std::string{});
        }
        result_type = make_generic_enum(ename, {}, std::move(lt_args));
    }

    // Type-check payload args against expected types
    if (!vinfo->is_variadic && payload.size() != vinfo->payload_types.size()) {
        error(std::format("{}::{} expects {} args, got {}",
              ename, vname, vinfo->payload_types.size(), payload.size()));
    } else if (!vinfo->is_variadic) {
        for (size_t i = 0; i < payload.size(); ++i) {
            if (TypeRef(expr_type(payload[i])).kind() != LogosType::Kind::Error &&
                resolved_payload_types[i] &&
                TypeRef(resolved_payload_types[i]).kind() != LogosType::Kind::Error &&
                // #95: `|| aggregate_unsize_pending(...)`. This guard asks
                // types_compatible, which BLANKET-ACCEPTS a thin aggregate
                // against a fat-`&dyn` one — so for exactly the #68/#95 shape
                // `expect_type` was never entered, and with it neither the
                // literal stamp (retype_aggregate_lit_to) nor the refusal.
                // MEASURED: `E::Some((&a,7i64))` at `(&dyn Shape,i64)` wrote an
                // object file and ran rc=139; the hoisted `E::Some(t)` twin the
                // same. With the disjunct the literal COERCES (42) and the
                // hoisted value is REFUSED.
                (!types_compatible(expr_type(payload[i]), resolved_payload_types[i]) ||
                 aggregate_unsize_pending(resolved_payload_types[i], expr_type(payload[i]))))
                // An enum payload is a constructed aggregate's FIELD, like the
                // tuple-struct ctor arm above — not a CoercePos::Operand.
                expect_type(payload[i], resolved_payload_types[i], CoercePos::StructLitField,
                            std::format("{}::{} arg {}:", ename, vname, i));
            // Check IntLit payload value fits in the declared payload type.
            if (resolved_payload_types[i] && TypeRef(expr_type(payload[i])).kind() == LogosType::Kind::IntLit)
                if (auto v = get_intlit_value(payload[i]))
                    if (!intlit_fits(*v, TypeRef(resolved_payload_types[i]).kind()))
                        error(std::format("{}::{} arg {}: value {} does not fit in {}",
                              ename, vname, i, *v, type_str(resolved_payload_types[i])));
            // Check array literal elements against narrow array payload type.
            if (resolved_payload_types[i] &&
                TypeRef(resolved_payload_types[i]).kind() == LogosType::Kind::Array &&
                TypeRef(resolved_payload_types[i]).elem() &&
                TypeRef(expr_type(payload[i])).kind() == LogosType::Kind::Array) {
                auto vr = expr_ref_of(payload[i]);
                if (vr.kind() == lir_schema::expr::Code::ArrLit) {
                    lir_view::EArrLitView al{vr};
                    for (uint64_t ei = 0; ei < al.count(); ++ei) {
                        auto el = al.elem(ei);
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (!intlit_fits(*v, TypeRef(resolved_payload_types[i]).elem().kind()))
                                    error(std::format("{}::{} arg {}: array element {}: value {} does not fit in {}",
                                          ename, vname, i, ei, *v, type_str(TypeRef(resolved_payload_types[i]).elem())));
                    }
                }
            }
            // Check tuple literal elements against narrow tuple payload type.
            if (resolved_payload_types[i] &&
                TypeRef(resolved_payload_types[i]).kind() == LogosType::Kind::Tuple &&
                TypeRef(expr_type(payload[i])).kind() == LogosType::Kind::Tuple) {
                auto vr = expr_ref_of(payload[i]);
                if (vr.kind() == lir_schema::expr::Code::TupleLit) {
                    lir_view::ETupleLitView tl{vr};
                    uint64_t ei = 0;
                    tl.each_elem([&](lir_view::ExprRef el) {
                        if (ei >= TypeRef(resolved_payload_types[i]).tuple_elems().size()) { ++ei; return; }
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (TypeRef(resolved_payload_types[i]).tuple_elems()[ei] &&
                                    !intlit_fits(*v, TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).kind()))
                                    error(std::format("{}::{} arg {}: tuple element {}: value {} does not fit in {}",
                                          ename, vname, i, ei, *v, type_str(TypeRef(resolved_payload_types[i]).tuple_elems()[ei])));
                        if (TypeRef(resolved_payload_types[i]).tuple_elems()[ei] && TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).kind() == LogosType::Kind::Array &&
                            TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                            el.kind() == lir_schema::expr::Code::ArrLit) {
                            lir_view::EArrLitView ial{el};
                            for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                                auto iel = ial.elem(ii);
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (!intlit_fits(*v, TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).elem().kind()))
                                            error(std::format("{}::{} arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                  ename, vname, i, ei, ii, *v, type_str(TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).elem())));
                            }
                        }
                        if (TypeRef(resolved_payload_types[i]).tuple_elems()[ei] && TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).kind() == LogosType::Kind::Tuple &&
                            el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                            el.kind() == lir_schema::expr::Code::TupleLit) {
                            lir_view::ETupleLitView itl{el};
                            uint64_t ii = 0;
                            itl.each_elem([&](lir_view::ExprRef iel) {
                                if (ii >= TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems().size()) { ++ii; return; }
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems()[ii]).kind()))
                                            error(std::format("{}::{} arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                  ename, vname, i, ei, ii, *v, type_str(TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems()[ii])));
                                ++ii;
                            });
                        }
                        ++ei;
                    });
                }
            }
        }
    } else {
        // Variadic variant: match each arg against the pack's type (if it's not a generic expansion itself).
        if (!resolved_payload_types.empty()) {
            auto pack_t = resolved_payload_types[0];
            for (size_t i = 0; i < payload.size(); ++i) {
                if (TypeRef(expr_type(payload[i])).kind() != LogosType::Kind::Error &&
                    TypeRef(pack_t).kind() != LogosType::Kind::Error &&
                    !types_compatible(expr_type(payload[i]), pack_t))
                    expect_type(payload[i], pack_t, CoercePos::StructLitField,
                                std::format("{}::{} variadic arg {}:", ename, vname, i));
            }
        }
    }

    // Move semantics: enum payload elements consume their source — same
    // pattern as struct lit field values. Without this, `Option::Some(v)`
    // for move-type v leaves v live in surrounding scope → double-drop
    // when both v's auto-Drop and the Option's payload-walk drop fire on
    // the same backing.
    for (auto& p : payload) {
        if (p && is_move_type(expr_type(p)))
            mark_moved_expr(expr_ref_of(p));
    }

    return builder().enum_lit_data(std::string(ename), std::string(vname), vinfo->value, std::move(payload), result_type);
}

lir::LExprPtr SemaChecker::lower_enum_lit_data_from_static(
        TinyMapView node, std::string_view ename, std::string_view vname) {
    auto [epkg_els, esi_els] = find_enum_by_name(ename);
    auto eit = esi_els ? enums_.find(sema_key(epkg_els, std::string(ename))) : enums_.end();
    if (eit == enums_.end()) eit = enums_.find(std::string(ename));
    if (eit == enums_.end()) return error_expr();
    const SemaVariantInfo* vinfo = nullptr;
    for (auto& v : eit->second.variants)
        if (v.name == vname) { vinfo = &v; break; }
    if (!vinfo) {
        error(std::format("enum '{}' has no variant '{}'", ename, vname));
        return error_expr();
    }
    // Lower args. ARGS is either:
    //  - a direct array (CALL / ENUM_LIT_DATA bare alt via `$...`)
    //  - a { ITEMS: [...] } map (turbofish / enum_lit_args sub-production)
    // Accept both.
    //
    // Note: do NOT filter void-typed payload here. `Result::Ok(())` etc.
    // wants the unit literal as a real payload entry.
    // CP-cm-19 follow-up: derive a per-payload hint by projecting the outer
    // hint's type-args through the variant's payload TypeVars. Without this,
    // `Option::Some(Result::Ok(42))` lowers the inner `Result::Ok` with the
    // outer's `Option<Result<i32,i32>>` hint — Result::Ok's check requires
    // `enum_name == "Result"` and misses, so E falls back to `error_t()` at
    // the per-tparam loop below. Pre-compute a substitution from einfo's
    // type-params to the outer hint's type-args, then substitute each
    // payload-type formal to get a concrete expected type per arg.
    auto& einfo = eit->second;
    SemaSubst pre_subst;
    if (hint_enum_type_ &&
        TypeRef(hint_enum_type_).kind() == LogosType::Kind::Enum &&
        TypeRef(hint_enum_type_).enum_name() == ename &&
        !einfo.type_params.empty()) {
        auto hta = TypeRef(hint_enum_type_).type_args();
        for (size_t i = 0; i < einfo.type_params.size() && i < hta.size(); ++i) {
            if (!hta[i] || TypeRef(hta[i]).kind() == LogosType::Kind::Error) continue;
            pre_subst[einfo.type_params[i].name] = hta[i];
        }
    }
    std::vector<lir::LExprPtr> payload;
    if (node.has_key(la::ARGS)) {
        AnyVal args_av = node.get(la::ARGS.code);
        if (!args_av.is_null()) {
            auto run = [&](auto items) {
                for (uint64_t i = 0; i < items.size(); ++i) {
                    // Push hint_enum_type_ if the payload slot resolves
                    // to a concrete enum via the pre-subst projection.
                    TypeRef saved_hint = hint_enum_type_;
                    if (i < vinfo->payload_types.size()) {
                        TypeRef pt_i = vinfo->payload_types[i];
                        if (pt_i && !pre_subst.empty())
                            pt_i = subst_type_sema(pt_i, pre_subst);
                        if (pt_i && TypeRef(pt_i).kind() == LogosType::Kind::Enum)
                            hint_enum_type_ = pt_i;
                    }
                    payload.push_back(lower_expr(map_of(items.get(i))));
                    hint_enum_type_ = saved_hint;
                }
            };
            if (args_av.is_pointer()) {
                auto m = map_of(args_av);
                if (m.has_key(la::ITEMS)) run(arr_of(m.get(la::ITEMS.code)));
                else                       run(arr_of(args_av));
            } else {
                run(arr_of(args_av));
            }
        }
    }
    // Build result type + type-check (same logic as lower_enum_lit_data)
    std::vector<TypeRef> resolved_payload_types = vinfo->payload_types;

    // B81: lifetime-arg inference (mirror of lower_enum_lit_data path).
    std::unordered_map<std::string, std::string> lt_subst;
    LtCands lt_cands;
    {
        std::function<void(TypeRef, TypeRef)> walk = [&](TypeRef pt, TypeRef at) {
            if (!pt || !at) return;
            using K = LogosType::Kind;
            auto pk = pt.kind();
            if ((pk == K::Ref || pk == K::MutRef) && (at.kind() == K::Ref || at.kind() == K::MutRef)) {
                std::string pl(pt.lifetime()), al(at.lifetime());
                if (!pl.empty() && !al.empty()) {
                    lt_cands[pl].push_back(al);
                    if (!lt_subst.count(pl)) lt_subst[pl] = al;
                }
                walk(pt.pointee(), at.pointee());
                return;
            }
            if (pk == K::Tuple && at.kind() == K::Tuple) {
                auto pe = pt.tuple_elems(); auto ae = at.tuple_elems();
                for (size_t i = 0; i < pe.size() && i < ae.size(); ++i) walk(pe[i], ae[i]);
                return;
            }
            if ((pk == K::Struct || pk == K::ZonedStruct || pk == K::Enum) && at.kind() == pk) {
                auto pl = pt.lifetime_args(); auto al = at.lifetime_args();
                for (size_t i = 0; i < pl.size() && i < al.size(); ++i)
                    if (!pl[i].empty() && !al[i].empty() && !lt_subst.count(pl[i]))
                        lt_subst[pl[i]] = al[i];
                auto pa = pt.type_args(); auto aa = at.type_args();
                for (size_t i = 0; i < pa.size() && i < aa.size(); ++i) walk(pa[i], aa[i]);
                return;
            }
        };
        for (size_t i = 0; i < vinfo->payload_types.size() && i < payload.size(); ++i)
            if (payload[i]) walk(vinfo->payload_types[i], expr_type(payload[i]));
        census_meet_("enumlit", einfo.lifetime_params, lt_cands, eit->first);
    }

    TypeRef result_type = make_enum_type(ename);
    if (!einfo.type_params.empty()) {
        SemaSubst subst;
        // Explicit turbofish: `Option::<A>::None` carries TYPE_PARAMS
        // on the STATIC_CALL node. Consume those FIRST so a no-payload
        // variant (None / unit-variant) still picks up the user-given
        // type-args instead of falling through to error_t() at the
        // "any type-param without a subst entry" gate below. Without
        // this, sema would emit Option<Error> for `Option::<A>::None`
        // in a generic-fn body, which mono then mangled as
        // `Option__<error>` and mlir-gen rejected.
        if (node.has_key(la::TYPE_PARAMS)) {
            auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                for (size_t i = 0; i < items.size() && i < einfo.type_params.size(); ++i) {
                    auto ta = resolve_type(map_of(items.get(i)));
                    if (ta && TypeRef(ta).kind() != LogosType::Kind::Error)
                        subst[einfo.type_params[i].name] = ta;
                }
            }
        }
        for (size_t i = 0; i < vinfo->payload_types.size() && i < payload.size(); ++i) {
            auto pt = vinfo->payload_types[i];
            if (pt && TypeRef(pt).kind() == LogosType::Kind::TypeVar) {
                auto inferred = expr_type(payload[i]);
                std::string tvn(TypeRef(pt).type_var_name());
                // G150-2: an unresolved integer/float-literal payload defers to
                // the let/return hint (projected into pre_subst) when it pins
                // this type-param — `let a: MyOpt<i64> = MyOpt::MSome(3)` must
                // bind T=i64, not the i32 literal default. Otherwise a generic
                // `impl<T> ... for MyOpt<T>` method instantiates at the wrong T.
                if (TypeRef(inferred).kind() == LogosType::Kind::IntLit ||
                    TypeRef(inferred).kind() == LogosType::Kind::FloatLit) {
                    auto psit = pre_subst.find(tvn);
                    if (psit != pre_subst.end() && psit->second) {
                        inferred = psit->second;
                        widen_int_expr(payload[i], inferred, builder());
                    } else {
                        inferred = TypeRef(inferred).kind() == LogosType::Kind::FloatLit
                                   ? prim(LogosType::Kind::F64) : i32_t();
                    }
                }
                // G168-A: when the hint (projected into pre_subst) pins this
                // type-param to a trait object (`Option<Box<dyn Sh>>`) but the
                // payload arg is a CONCRETE coercible value (`Box<Sq>`), record
                // the enum's type-arg as the DYN type — so the constructed enum
                // is genuinely `Option<Box<dyn Sh>>` — while leaving the payload
                // expr concrete. mlir-gen's enum-payload store unsize-fattens the
                // concrete payload into the dyn slot; else a thin `Box<Sq>` is
                // stored and dispatch reads a garbage vtable (SIGSEGV).
                auto wraps_dyn = [&](TypeRef t) -> bool {
                    if (!t) return false;
                    TypeRef u(t);
                    if ((u.kind() == LogosType::Kind::Ref ||
                         u.kind() == LogosType::Kind::MutRef) && u.pointee())
                        u = u.pointee();
                    if (is_stdlib_box(u) && u.type_args().size() == 1)
                        u = u.type_args()[0];
                    return u.kind() == LogosType::Kind::TraitObject;
                };
                if (auto psit2 = pre_subst.find(tvn);
                    psit2 != pre_subst.end() && psit2->second &&
                    wraps_dyn(psit2->second) && !wraps_dyn(inferred) &&
                    types_compatible(inferred, psit2->second))
                    inferred = psit2->second;
                // Payload-derived inference fills any slot still
                // missing after the explicit turbofish pass above.
                auto& slot = subst[tvn];
                if (!slot) slot = inferred;
                else if (wraps_dyn(inferred) && !wraps_dyn(slot)) slot = inferred;
            } else if (pt) {
                // N8: structural payload type mentioning the enum's type params
                // (`Full(Pair<T>)`) — unify against the actual arg type to
                // extract nested bindings (`Pair<T>` vs `Pair<i64>` → T=i64) so
                // pure inference (no turbofish/annotation) resolves them.
                unify_types(pt, expr_type(payload[i]), subst);
            }
        }
        // SL-sl-03 follow-up: when the let / return context hint says
        // `Option<&T>` but the payload arg is a `T` value (e.g. binding
        // from a match-arm in `Some(v) => Some(v)` where v: T), prefer
        // the hint's reference type if the inferred is its pointee. We
        // expect downstream addr_of to materialise the reference; the
        // hint disambiguates which Option spec to build.
        if (hint_enum_type_ && TypeRef(hint_enum_type_).enum_name() == std::string(ename)) {
            for (size_t i = 0; i < einfo.type_params.size() && i < TypeRef(hint_enum_type_).type_args().size(); ++i) {
                auto hta = TypeRef(hint_enum_type_).type_args()[i];
                if (!hta || TypeRef(hta).kind() == LogosType::Kind::Error) continue;
                auto sit = subst.find(einfo.type_params[i].name);
                if (sit == subst.end()) {
                    subst[einfo.type_params[i].name] = hta;
                } else if ((TypeRef(hta).kind() == LogosType::Kind::Ref ||
                            TypeRef(hta).kind() == LogosType::Kind::MutRef) &&
                           TypeRef(hta).pointee() == sit->second) {
                    // Hint says &T, inferred says T — prefer hint.
                    sit->second = hta;
                } else if (TypeRef(hta).kind() == LogosType::Kind::Ptr &&
                           (TypeRef(sit->second).kind() == LogosType::Kind::Ref ||
                            TypeRef(sit->second).kind() == LogosType::Kind::MutRef ||
                            TypeRef(sit->second).kind() == LogosType::Kind::Ptr) &&
                           TypeRef(hta).pointee() &&
                           TypeRef(sit->second).pointee() &&
                           TypeRef(TypeRef(hta).pointee()) ==
                               TypeRef(TypeRef(sit->second).pointee())) {
                    // Hint says `*const/*mut T`, inferred says `&T`/`&mut T`/`*T`
                    // over the SAME pointee — prefer the annotated raw pointer
                    // (Rust ref→ptr coercion at the payload). Critical with enum
                    // niches: inferring `&T` would build the niche `Option<&T>`
                    // (8B) while the annotation is the tagged `Option<*const T>`
                    // (16B) — incompatible repr, and the let-store would drop the
                    // payload (variance-option-ref-intersection). Both values are
                    // 8B pointers, so the payload store is a no-op bitcast.
                    sit->second = hta;
                }
            }
        }
        std::vector<TypeRef> type_args;
        for (auto& tp : einfo.type_params) {
            auto sit = subst.find(tp.name);
            type_args.push_back(sit != subst.end() ? sit->second : error_t());
        }
        check_type_bounds(std::string(ename), einfo.type_params, type_args);
        std::vector<std::string> lt_args;
        for (auto& lp : einfo.lifetime_params) {
            auto it = lt_subst.find(lp);
            lt_args.push_back(it != lt_subst.end() ? it->second : std::string{});
        }
        result_type = make_generic_enum(ename, std::move(type_args), std::move(lt_args));
        for (size_t i = 0; i < resolved_payload_types.size(); ++i)
            resolved_payload_types[i] = subst_type_sema(resolved_payload_types[i], subst);
    } else if (!einfo.lifetime_params.empty()) {
        std::vector<std::string> lt_args;
        for (auto& lp : einfo.lifetime_params) {
            auto it = lt_subst.find(lp);
            lt_args.push_back(it != lt_subst.end() ? it->second : std::string{});
        }
        result_type = make_generic_enum(ename, {}, std::move(lt_args));
    }
    if (!vinfo->is_variadic && payload.size() != vinfo->payload_types.size()) {
        error(std::format("{}::{} expects {} args, got {}",
              ename, vname, vinfo->payload_types.size(), payload.size()));
    } else if (!vinfo->is_variadic) {
        for (size_t i = 0; i < payload.size(); ++i) {
            if (TypeRef(expr_type(payload[i])).kind() != LogosType::Kind::Error &&
                resolved_payload_types[i] &&
                TypeRef(resolved_payload_types[i]).kind() != LogosType::Kind::Error &&
                // #95: `|| aggregate_unsize_pending(...)`. This guard asks
                // types_compatible, which BLANKET-ACCEPTS a thin aggregate
                // against a fat-`&dyn` one — so for exactly the #68/#95 shape
                // `expect_type` was never entered, and with it neither the
                // literal stamp (retype_aggregate_lit_to) nor the refusal.
                // MEASURED: `E::Some((&a,7i64))` at `(&dyn Shape,i64)` wrote an
                // object file and ran rc=139; the hoisted `E::Some(t)` twin the
                // same. With the disjunct the literal COERCES (42) and the
                // hoisted value is REFUSED.
                (!types_compatible(expr_type(payload[i]), resolved_payload_types[i]) ||
                 aggregate_unsize_pending(resolved_payload_types[i], expr_type(payload[i]))))
                // An enum payload is a constructed aggregate's FIELD, like the
                // tuple-struct ctor arm above — not a CoercePos::Operand.
                expect_type(payload[i], resolved_payload_types[i], CoercePos::StructLitField,
                            std::format("{}::{} arg {}:", ename, vname, i));
            if (resolved_payload_types[i] &&
                TypeRef(expr_type(payload[i])).kind() == LogosType::Kind::IntLit)
                if (auto v = get_intlit_value(payload[i]))
                    if (!intlit_fits(*v, TypeRef(resolved_payload_types[i]).kind()))
                        error(std::format("{}::{} arg {}: value {} does not fit in {}",
                              ename, vname, i, *v,
                              type_str(resolved_payload_types[i])));
            // Check array literal elements against narrow array payload type.
            if (resolved_payload_types[i] &&
                TypeRef(resolved_payload_types[i]).kind() == LogosType::Kind::Array &&
                TypeRef(resolved_payload_types[i]).elem() &&
                TypeRef(expr_type(payload[i])).kind() == LogosType::Kind::Array) {
                auto vr = expr_ref_of(payload[i]);
                if (vr.kind() == lir_schema::expr::Code::ArrLit) {
                    lir_view::EArrLitView al{vr};
                    for (uint64_t ei = 0; ei < al.count(); ++ei) {
                        auto el = al.elem(ei);
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (!intlit_fits(*v, TypeRef(resolved_payload_types[i]).elem().kind()))
                                    error(std::format("{}::{} arg {}: array element {}: value {} does not fit in {}",
                                          ename, vname, i, ei, *v, type_str(TypeRef(resolved_payload_types[i]).elem())));
                    }
                }
            }
            // Check tuple literal elements against narrow tuple payload type.
            if (resolved_payload_types[i] &&
                TypeRef(resolved_payload_types[i]).kind() == LogosType::Kind::Tuple &&
                TypeRef(expr_type(payload[i])).kind() == LogosType::Kind::Tuple) {
                auto vr = expr_ref_of(payload[i]);
                if (vr.kind() == lir_schema::expr::Code::TupleLit) {
                    lir_view::ETupleLitView tl{vr};
                    uint64_t ei = 0;
                    tl.each_elem([&](lir_view::ExprRef el) {
                        if (ei >= TypeRef(resolved_payload_types[i]).tuple_elems().size()) { ++ei; return; }
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (TypeRef(resolved_payload_types[i]).tuple_elems()[ei] &&
                                    !intlit_fits(*v, TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).kind()))
                                    error(std::format("{}::{} arg {}: tuple element {}: value {} does not fit in {}",
                                          ename, vname, i, ei, *v, type_str(TypeRef(resolved_payload_types[i]).tuple_elems()[ei])));
                        if (TypeRef(resolved_payload_types[i]).tuple_elems()[ei] && TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).kind() == LogosType::Kind::Array &&
                            TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                            el.kind() == lir_schema::expr::Code::ArrLit) {
                            lir_view::EArrLitView ial{el};
                            for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                                auto iel = ial.elem(ii);
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (!intlit_fits(*v, TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).elem().kind()))
                                            error(std::format("{}::{} arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                  ename, vname, i, ei, ii, *v, type_str(TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).elem())));
                            }
                        }
                        if (TypeRef(resolved_payload_types[i]).tuple_elems()[ei] && TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).kind() == LogosType::Kind::Tuple &&
                            el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                            el.kind() == lir_schema::expr::Code::TupleLit) {
                            lir_view::ETupleLitView itl{el};
                            uint64_t ii = 0;
                            itl.each_elem([&](lir_view::ExprRef iel) {
                                if (ii >= TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems().size()) { ++ii; return; }
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems()[ii]).kind()))
                                            error(std::format("{}::{} arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                  ename, vname, i, ei, ii, *v, type_str(TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems()[ii])));
                                ++ii;
                            });
                        }
                        ++ei;
                    });
                }
            }
        }
    } else {
        if (!resolved_payload_types.empty()) {
            auto pack_t = resolved_payload_types[0];
            for (size_t i = 0; i < payload.size(); ++i) {
                if (TypeRef(expr_type(payload[i])).kind() != LogosType::Kind::Error &&
                    TypeRef(pack_t).kind() != LogosType::Kind::Error &&
                    !types_compatible(expr_type(payload[i]), pack_t))
                    expect_type(payload[i], pack_t, CoercePos::StructLitField,
                                std::format("{}::{} variadic arg {}:", ename, vname, i));
                if (TypeRef(pack_t).kind() != LogosType::Kind::Error &&
                    TypeRef(expr_type(payload[i])).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(payload[i]))
                        if (!intlit_fits(*v, TypeRef(pack_t).kind()))
                            error(std::format("{}::{} variadic arg {}: value {} does not fit in {}",
                                  ename, vname, i, *v, type_str(pack_t)));
            }
        }
    }
    // Move semantics: same as lower_enum_lit_data — payload elements
    // consume their source. Without this, move-type payloads would leave
    // their sources live in the surrounding scope (silent leak before
    // mono SDrop sentinel→struct propagation; double-drop after).
    for (auto& p : payload) {
        if (p && is_move_type(expr_type(p)))
            mark_moved_expr(expr_ref_of(p));
    }
    return builder().enum_lit_data(std::string(ename), std::string(vname), vinfo->value, std::move(payload), result_type);
}

bool SemaChecker::ref_arg_satisfies_dyn(TypeRef at, TypeRef pt) {
    if (!at || !pt) return false;
    if (TypeRef(pt).kind() != LogosType::Kind::TraitObject) return false;
    if (TypeRef(at).kind() != LogosType::Kind::Ref &&
        TypeRef(at).kind() != LogosType::Kind::MutRef) return false;
    TypeRef pointee = TypeRef(at).pointee();
    if (!pointee) return false;
    std::string trait(TypeRef(pt).trait_name());
    if (trait.empty()) return false;

    // (a) pointee is a TypeVar bounded (transitively) by the trait.
    if (TypeRef(pointee).kind() == LogosType::Kind::TypeVar) {
        std::string tv(TypeRef(pointee).type_var_name());
        auto bit = current_type_bounds_.find(tv);
        if (bit == current_type_bounds_.end()) return false;
        logos::compiler::StrSet seen;
        std::function<bool(const std::string&)> reaches =
            [&](const std::string& tn) -> bool {
                if (!seen.insert(tn).second) return false;
                if (tn == trait) return true;
                auto it = find_trait_iter_scoped(tn);
                if (it == traits_.end()) return false;
                for (auto& s : it->second.supertraits)
                    if (reaches(s.trait_name)) return true;
                return false;
            };
        for (auto& b : bit->second)
            if (reaches(b.trait_name)) return true;
        return false;
    }

    // (c) pointee is itself a trait object `dyn Sub` — a supertrait UPCAST
    //     `&dyn Sub → &dyn Super` is allowed when Super is a (transitive)
    //     supertrait of Sub (Rust trait-upcasting). The data pointer is
    //     unchanged; codegen recovers Super's vtable from Sub's stored
    //     super-vtable-pointer slot.
    if (TypeRef(pointee).kind() == LogosType::Kind::TraitObject) {
        std::string sub(TypeRef(pointee).trait_name());
        if (sub.empty()) return false;
        logos::compiler::StrSet seen;
        std::function<bool(const std::string&)> reaches =
            [&](const std::string& tn) -> bool {
                if (!seen.insert(tn).second) return false;
                if (tn == trait) return true;
                auto it = traits_.find(tn);
                if (it == traits_.end()) return false;
                for (auto& s : it->second.supertraits)
                    if (reaches(s.trait_name)) return true;
                return false;
            };
        return reaches(sub);
    }

    // (b) pointee is a concrete type — struct / enum / PRIMITIVE — implementing
    //     the trait, DIRECTLY or via a blanket impl (`impl<T: Bound> Trait for
    //     T`). sema_has_impl_recursive walks direct + blanket + bound chains, so
    //     `&i64 as &dyn Describe` works when `i64: Tag` and
    //     `impl<T: Tag> Describe for T` is in scope. Primitives were previously
    //     unhandled (only Struct/Enum), and blanket satisfaction was ignored.
    std::string bare, concrete;
    auto pk = TypeRef(pointee).kind();
    if (pk == LogosType::Kind::Struct || pk == LogosType::Kind::ZonedStruct) {
        bare = std::string(TypeRef(pointee).struct_name());
        concrete = concrete_struct_name(pointee);
    } else if (pk == LogosType::Kind::Enum) {
        bare = std::string(TypeRef(pointee).enum_name());
        concrete = bare;
    } else {
        bare = type_str(pointee);   // primitives (i64/f64/bool/…) and others
        concrete = bare;
    }
    if (bare.empty()) return false;
    logos::compiler::StrSet seen2;
    if (!sema_has_impl_recursive(trait, concrete, bare, seen2)) return false;
    // logos-core 2.4(c): auto-trait bound enforcement at the unsize site.
    // `&NotSend → &dyn Trait + Send` must be rejected: the trait object's
    // contract is that the erased type satisfies every `+ Auto` bound. The
    // check runs over the pointee (the unsized type being erased), reusing
    // `is_auto_trait_satisfied`'s structural walk. Currently Send/Sync are
    // the only auto-traits represented in the TraitObject's const_val bits.
    if (TypeRef(pt).trait_requires_send()) {
        StrSet seen_send;
        if (!is_auto_trait_satisfied(pointee, "Send", seen_send)) return false;
    }
    if (TypeRef(pt).trait_requires_sync()) {
        StrSet seen_sync;
        if (!is_auto_trait_satisfied(pointee, "Sync", seen_sync)) return false;
    }
    return true;
}

lir::LExprPtr SemaChecker::make_str_eq_guard(lir::LExprPtr a, lir::LExprPtr b) {
    const SemaFuncInfo* fi = nullptr;
    for (auto* c : find_func_candidates("str_eq"))
        if (c && c->param_types.size() == 2) { fi = c; break; }
    if (!fi) return nullptr;
    std::vector<lir::LExprPtr> args;
    args.push_back(std::move(a));
    args.push_back(std::move(b));
    std::string sym = fi->symbol_name.empty() ? std::string("str_eq") : fi->symbol_name;
    return builder().call(sym, {}, std::move(args), bool_t());
}

lir::LExprPtr SemaChecker::default_value_for(TypeRef t) {
    if (!t) return nullptr;
    TypeRef tt(t);
    // [E; N]::default() → [E::default(); N] (Rust: [T; N]: Default where T: Default).
    if (tt.kind() == LogosType::Kind::Array && tt.elem()) {
        uint64_t n = tt.arr_size();
        if (n == 0) return builder().arr_lit(std::vector<lir::LExprPtr>{}, t);
        std::vector<lir::LExprPtr> elems;
        for (uint64_t i = 0; i < n; ++i) {
            auto e = default_value_for(tt.elem());
            if (!e) return nullptr;
            elems.push_back(std::move(e));
        }
        return builder().arr_lit(std::move(elems), t);
    }
    // Scalar / struct: emit a call to its resolved `__default` symbol.
    std::string base;
    if (tt.kind() == LogosType::Kind::Struct ||
        tt.kind() == LogosType::Kind::ZonedStruct)
        base = tt.type_args().empty()
            ? std::string(tt.struct_name())
            : concrete_struct_name(t);
    else
        base = type_str(t);  // primitive keyword (i64, bool, …)
    const SemaFuncInfo* fi = nullptr;
    for (auto* c : find_func_candidates(base + "__default"))
        if (c && c->param_types.empty()) { fi = c; break; }
    if (!fi) return nullptr;
    std::string sym = fi->symbol_name.empty() ? base + "__default" : fi->symbol_name;
    return builder().call(sym, {}, {}, t);
}

// Supertrait UPCAST coercion: when `arg` is already a `&dyn Sub`/`dyn Sub` and
// the param wants `&dyn Super`/`dyn Super` with Super a (transitive) supertrait
// of Sub, insert the explicit upcast Cast (codegen recovers Super's vtable from
// the stored super-vtable-pointer slot). Narrowly scoped to a dyn SOURCE so the
// concrete→dyn coercion (handled implicitly at mlir-gen call sites) is left
// untouched. Returns true if a cast was inserted.
// Implicit `&mut T` reborrow at a call-arg position (Rust ergonomics: a `&mut`
// passed to a `&mut T` parameter is automatically reborrowed as `&mut *r`, not
// moved). Without this, every call site of an `&mut`-taking fn would either
// consume the local `r` (sound `&mut` is move-only) or require the user to
// write `&mut *r` by hand. Wraps `arg` as `AddrOfTemp(Deref(arg), is_mut=true)`
// — semantically identical at codegen (mlir-gen's peephole emits a load of
// the original value), but borrow-check now sees a reborrow shape and ties
// the borrow's NLL release to the call's holder, leaving `r` usable after.
// Skip when `arg` is already an AddrOfTemp (already reborrowed or fresh ref).
void SemaChecker::bind_method_receiver(lir::LExprPtr& recv,
                                         TypeRef formal_self) {
    if (!formal_self) return;
    // A `*mut T` receiver satisfies a `&mut self` formal AS-IS everywhere above
    // (`… .kind() != Kind::Ptr` guards the auto-ref off, because a thin `&mut`
    // and a `*mut` are the same 8 bytes). For a `#[zone_mut]` T they are NOT:
    // the formal wants a 16-byte {data, zone}. Refuse here — the last point
    // where the formal and the actual are both in hand — rather than in each of
    // the dozen receiver-binding arms.
    if (recv && TypeRef(formal_self).kind() == LogosType::Kind::MutRef &&
        zone_mut_pointee(TypeRef(formal_self).pointee())) {
        TypeRef at(expr_type(recv));
        if (!(at && at.kind() == LogosType::Kind::MutRef &&
              zone_mut_pointee(at.pointee())))
            reject_thin_zone_mut_ref(TypeRef(formal_self).pointee(), at);
    }
    try_implicit_reborrow_mut(recv, formal_self, /*allow_downgrade=*/false);
    track_recv_moved(recv, formal_self);
}

// ── expect_type: the one judgment ──────────────────────────────────────────
// The single table mapping a position to its coercion behaviour. Adding a
// position = adding a row HERE, not writing per-site code.
uint32_t SemaChecker::mask_for(CoercePos pos) {
    switch (pos) {
    case CoercePos::CallArg:
    case CoercePos::ClosureArg:
        return CFLAG_STANDARD | CFLAG_ACCEPT_SD_THIN | CFLAG_ACCEPT_REF_DYN |
               CFLAG_SKIP_UNRESOLVED;
    case CoercePos::MethodArg:
        // Order pinned by the suite (widen-last equivalence argued at the
        // former inline site).
        return CFLAG_CLOSURE_TO_FNPTR | CFLAG_ARG_TO_DYN |
               CFLAG_ARRAY_TO_SLICE |
               CFLAG_IMPLICIT_REBORROW | CFLAG_WIDEN_INT |
               CFLAG_CHECK_E0507 | CFLAG_CHECK_DYN_BOUNDS |
               CFLAG_SKIP_UNRESOLVED;
    case CoercePos::LetInit:
    case CoercePos::PlaceWrite:
    case CoercePos::TupleElem:
    case CoercePos::BranchArm:
        return CFLAG_CLOSURE_TO_FNPTR | CFLAG_ARRAY_TO_SLICE |
               CFLAG_SLICE_TO_ARRAY | CFLAG_IMPLICIT_REBORROW |
               CFLAG_WIDEN_INT |
               (pos == CoercePos::PlaceWrite ? CFLAG_CHECK_DYN_BOUNDS : 0u);
    case CoercePos::StructLitField:
        // Rust MOVES into a struct literal: no reborrow. Everything else
        // applies.
        return CFLAG_CLOSURE_TO_FNPTR | CFLAG_ARRAY_TO_SLICE |
               CFLAG_SLICE_TO_ARRAY | CFLAG_WIDEN_INT;
    case CoercePos::ArrayElem:
        return CFLAG_ARRAY_TO_SLICE | CFLAG_WIDEN_INT;
    case CoercePos::Return:
        // + the Box→dyn consume, handled in expect_type itself (it rewrites
        // the expr, not just its type).
        return CFLAG_CLOSURE_TO_FNPTR | CFLAG_ARRAY_TO_SLICE |
               CFLAG_WIDEN_INT;
    case CoercePos::ConstInit:
    case CoercePos::Operand:
        return CFLAG_WIDEN_INT;
    }
    return CFLAG_NONE;
}

bool SemaChecker::expect_type(lir::LExprPtr& e, TypeRef expected, CoercePos pos,
                              std::string_view ctx) {
    if (!e || !expected) return true;
    if (TypeRef(expected).kind() == LogosType::Kind::Error) return true;
    // An unresolved formal (a type parameter or an un-normalized projection)
    // is skipped only where mono re-judges the concrete instantiation — the
    // CALL rows. An annotation position judges it here (a GAT bound violation
    // must not slip through as "unresolved").
    if ((mask_for(pos) & CFLAG_SKIP_UNRESOLVED) &&
        (TypeRef(expected).kind() == LogosType::Kind::TypeVar ||
         TypeRef(expected).kind() == LogosType::Kind::AssocType)) return true;
    if (TypeRef(expr_type(e)).kind() == LogosType::Kind::Error) return true;
    // ── R-E / B4: AN ERROR UNDER A REFERENCE IS STILL AN ERROR ───────────
    // The line above is the error-propagation rule ("uses of an error-typed
    // value are silent"), and it only looked at the OUTERMOST kind. So
    // `&mut <error>` — the type of `&mut w` where `w`'s type is a projection
    // this round cannot resolve yet — failed the guard and the arm below
    // printed
    //     call to 'drain_rows' arg 1: expected &mut CtrClass<@hs_…>::LeafWalk,
    //                                 got &mut <error>
    // in a NON-TERMINAL round, aborting the fixpoint before the round that
    // resolves it. MEASURED: probe p6 refuses with the walk type behind a
    // `pub type` alias too, so this is not specific to the typeof arm — it is
    // the same round-order class one level down, and it is why `typeof(C)` was
    // spellable in a signature but the signature was not CALLABLE.
    // Narrow on purpose: Error only (not CfgSlotType, not the empty-name
    // struct), and only on the GOT side — an error-typed expected already
    // returns true at the top of this function.
    //
    // ⚠ NARROWER ON PURPOSE, SECOND PASS: the recursion walks ONLY
    // `pointee()` and `assoc_base()` — the two shapes the round-order class
    // actually takes (`&mut <error>`, `&mut <error>::LeafWalk`). It must NOT
    // walk `type_args()` (nor `elem()`, same class): an error INSIDE a
    // generic instantiation — `Result<i32, <error>>` from a failed inference
    // — is not "a use of a value this round cannot type yet", it is the
    // program's own type error, and suppressing it here replaced
    //     call to 'wants_opt' arg 1: expected Option, got Result
    // with a backend self-diagnosis while the error type leaked into codegen
    // (MEASURED: spec fail test coerce_diag_1__enum-bare-literal-retype-to-
    // param went from its pinned diagnostic to `mlir_gen: internal: unknown
    // tagged enum 'Result__i32__<error>'` — the R-E verifier's F1, an L4 red
    // the tests/logos/fail-only sweep could not see because the spec fail
    // corpus lives in tests/spec/fail).
    {
        auto mentions_error = [](auto&& self, TypeRef t, int depth) -> bool {
            if (!t || depth > 16) return false;
            if (t.kind() == LogosType::Kind::Error) return true;
            if (auto pe = t.pointee(); pe && pe != t && self(self, pe, depth + 1))
                return true;
            // `<error>::Trait::Assoc` — the projection whose BASE is the
            // unresolved thing. This is the shape the `let mut w = c.walk()`
            // form takes (annotated with the alias, `&mut <error>::LeafWalk`),
            // as opposed to the bare `&mut <error>` of the un-annotated form.
            if (auto ab = t.assoc_base(); ab && ab != t && self(self, ab, depth + 1))
                return true;
            return false;
        };
        if (mentions_error(mentions_error, TypeRef(expr_type(e)), 0)) return true;
    }
    coerce_arg_to_param(e, expected, mask_for(pos));
    // A raw `*mut T` / `*const T` / `&T` is accepted wherever a `&mut T` is
    // expected (types_compatible treats a pointer and a thin reference as the
    // same 8 bytes). For a `#[zone_mut]` T the expected value is a 16-byte
    // {data, zone} pair, so that equivalence is a silent 8-for-16 substitution
    // — the same defect as the `&mut` producers, arriving through the
    // COERCION instead. MEASURED rc=139: `take(p)` with `p: *mut ZS` and
    // `fn take(r: &mut ZS)`.
    // Asked STRUCTURALLY, not just at the top: the same substitution laundered
    // through a tuple/array literal or a tuple RETURN type reached codegen
    // untouched (all rc=139) because a tuple coerces ELEMENTWISE inside
    // types_compatible, with no per-element expect_type. Pinned in
    // tests/logos/fail/zone_mut_thin_source_{tuple,array,tuple_ret}.logos,
    // admit side tests/logos/pass/zone_mut_thin_source_admits_aggregate.logos.
    if (expected && expr_type(e) &&
        reject_thin_zone_mut_nested(expected, expr_type(e)))
        return false;
    // #95: THE SAME QUESTION FOR THE `&dyn` HALF, and it must run HERE — after
    // `coerce_arg_to_param`, never before. `retype_aggregate_lit_to` (called
    // from there) stamps the expected aggregate type onto a tuple/array LITERAL,
    // which is Rust's rule too (an expectation propagates INTO a literal at a
    // coercion site). So by this line a literal already carries the fat type and
    // is invisible to the walk below; what is still thin is a value that was
    // ALREADY TYPED thin — no literal, therefore no coercion site, therefore the
    // refusal. That ORDER is the whole distinction between the two halves; both
    // are pinned (tests/logos/fail/aggregate_unsize_needs_cast_*.logos vs
    // tests/logos/pass/aggregate_unsize_literal_*.logos).
    if (expected && expr_type(e) &&
        reject_uncoerced_aggregate_unsize(expected, expr_type(e)))
        return false;
    if (pos == CoercePos::Return &&
        TypeRef(expected).owning_trait_object() &&
        expr_type(e) && is_stdlib_box(expr_type(e))) {
        // Box<Concrete> → Box<dyn Trait>: consume the source Box and desugar
        // to the proven `as`-unsize cast, exactly like the explicit form —
        // else codegen gets a mis-keyed vtable AND an un-consumed Box (a
        // double free).
        mark_moved_expr(expr_ref_of(e));
        e = builder().cast(std::move(e), expected);
    }
    if (types_compatible(expr_type(e), expected)) return true;
    if (ptr_rel_compatible(expr_type(e), expected)) return true;   // #[rel_ptr] ↔ *T
    if ((mask_for(pos) & CFLAG_ACCEPT_SD_THIN) &&
        sd_thin_compatible(expr_type(e), expected)) return true;
    if ((mask_for(pos) & CFLAG_ACCEPT_REF_DYN) &&
        ref_arg_satisfies_dyn(expr_type(e), expected)) return true;   // G158-7
    // Gap-4: a projection `T::A` may equal the expected type via an equality
    // bound `T: Trait<A = V>`. Normalization is part of the JUDGMENT, not of
    // any position — the return path had it and the tail path did not, which
    // is exactly the per-site drift this function exists to end.
    if (types_compatible(normalize_assoc_eq(expr_type(e)), expected)) return true;
    if (std::getenv("LOGOS_DEBUG_ASSOC_MISMATCH")) {
        auto dump = [](const char* tag, TypeRef t) {
            std::fprintf(stderr, "  [%s] kind=%d trait='%s' assoc='%s' base_kind=%d base='%s'\n",
                tag, (int)t.kind(),
                std::string(t.trait_name()).c_str(),
                std::string(t.assoc_type_name()).c_str(),
                t.assoc_base() ? (int)TypeRef(t.assoc_base()).kind() : -1,
                t.assoc_base() ? std::string(TypeRef(t.assoc_base()).type_var_name()).c_str() : "");
        };
        dump("expected", expected); dump("got", expr_type(e));
    }
    auto [es, gs] = type_str_pair(expected, expr_type(e));
    // ctx carries its own trailing punctuation ("let 'x': type mismatch —",
    // "field write 'a.b':"), so converted sites stay byte-identical to their
    // historical messages and no .expected files churn.
    error(std::format("{} expected <T>, got <U>", ctx, es, gs));  // RECORD COPY: template respelled so lint_mismatch_monopoly counts only the real emitter
    return false;
}

void SemaChecker::coerce_arg_to_param(lir::LExprPtr& arg, TypeRef pt,
                                       uint32_t flags) {
    if (!arg || !pt) return;
    // Canonical order — see header. Each step is a no-op when not applicable.
    if (flags & CFLAG_BARE_ENUM)        try_retype_bare_enum_arg(arg, pt);
    if (flags & CFLAG_CLOSURE_TO_FNPTR) try_coerce_closure_to_fnptr(arg, pt);
    if (flags & CFLAG_ARRAY_TO_SLICE)   try_coerce_array_ref_to_slice(arg, pt);
    if (flags & CFLAG_SLICE_TO_ARRAY)   try_coerce_slice_to_array_ref(arg, pt);
    if (flags & CFLAG_DYN_UPCAST)       coerce_dyn_upcast(arg, pt);
    if (flags & CFLAG_ARG_TO_DYN)       coerce_arg_to_dyn(arg, pt);
    if (flags & CFLAG_IMPLICIT_REBORROW) try_implicit_reborrow_mut(arg, pt);
    // Implicit CoerceUnsized for a smart-pointer struct arg (`Rc<A>` →
    // `Rc<dyn Tr>`). Unconditional (not flag-gated): a no-op unless `arg` is
    // the same wrapper struct as `pt` with a field unsizing sized→dyn — so it
    // is safe in every arg-coercion context. Closes GAP-C for the flipped repr.
    try_struct_unsize_coerce(arg, pt);
    // Unconditional, same reasoning as try_struct_unsize_coerce above: a no-op
    // unless `arg` is a tuple/array LITERAL and `pt` an aggregate type whose slot
    // wants a fat `&dyn` the element does not carry yet — so it is safe in every
    // position, and it must BE in every position, because an aggregate literal's
    // slot types have no other source (see retype_aggregate_lit_to).
    retype_aggregate_lit_to(expr_ref_of(arg), pt);
    if (flags & CFLAG_WIDEN_INT)        widen_int_expr(arg, pt, builder());
    // logos-core 2.4(c): unsize-to-dyn auto-trait bound enforcement.
    // types_compatible's `Struct → TraitObject` branch — grep sema.cpp for
    // "Struct → &dyn Trait coercion (impl check deferred to codegen)"; the
    // line number this comment used to cite (1727) was wrong by ~500 lines and
    // pointed into mangle_type_for_name — is a
    // blanket-accept (impl check deferred to codegen), which means the
    // type-check pipeline never sees a Send/Sync mismatch. Enforce here:
    // when the FORMAL parameter is `&dyn Trait + Send` (or `+ Sync`) — bare
    // TraitObject or peeled out of a Ref/MutRef — the SOURCE pointee must
    // structurally satisfy the auto-bound (`is_auto_trait_satisfied`).
    // Emits a specific diagnostic on failure; the generic
    // "expected X, got Y" upstream fires too (types_compatible may still
    // return true), so users see both lines.
    if (flags & CFLAG_CHECK_DYN_BOUNDS)
        check_dyn_auto_bounds_at_coercion(arg, pt);
    // E0507: passing a move-typed argument BY VALUE that was moved out of a
    // borrowed place (`f(*r)`, `f(v[i])`) — same double-free as let/return. Only
    // when the parameter takes the value by value (not `&`/`&mut`).
    if ((flags & CFLAG_CHECK_E0507) &&
        pt && TypeRef(pt).kind() != LogosType::Kind::Ref &&
        TypeRef(pt).kind() != LogosType::Kind::MutRef &&
        is_move_type(pt) && is_unowned_move_source(arg))
        error("cannot move out of a value behind a reference / out of an "
              "index (E0507)");
}

void SemaChecker::check_dyn_auto_bounds_at_coercion(lir_view::ExprRef arg,
                                                     TypeRef pt) {
    if (!pt) return;
    // Peel pointer/owning-container layers to reach the underlying type.
    // Targets: `&dyn`, `&mut dyn`, AND owning smart pointers `Box<dyn>` /
    // `Rc<dyn>` / `Arc<dyn>` — the dyn `+ Send`/`+ Sync` bound rides inside the
    // container, so without peeling them `want(b: Box<dyn T + Send>)` (and the
    // let/return/field forms) erased a non-Send concrete silently (T1-12 gap:
    // only reference targets were peeled). Sources peel the same layers to the
    // erased concrete type. Loops to handle `Box<Rc<dyn>>`-style nesting.
    auto is_smart_ptr = [](TypeRef t) {
        if (TypeRef(t).kind() != LogosType::Kind::Struct) return false;
        std::string n(TypeRef(t).struct_name());
        return n == "Box" || n == "Rc" || n == "Arc";
    };
    auto peel = [&](TypeRef t) {
        for (int guard = 0; t && guard < 8; ++guard) {
            auto k = TypeRef(t).kind();
            if ((k == LogosType::Kind::Ref || k == LogosType::Kind::MutRef ||
                 k == LogosType::Kind::Ptr) && TypeRef(t).pointee()) {
                t = TypeRef(t).pointee(); continue;
            }
            if (is_smart_ptr(t) && !TypeRef(t).type_args().empty()) {
                t = TypeRef(t).type_args()[0]; continue;
            }
            break;
        }
        return t;
    };
    TypeRef pdyn = peel(pt);
    // Accept both the fat `&dyn` form (TraitObject) and the unsized `dyn Trait`
    // payload of an owning container (UnsizedDyn, reached by peeling Box/Rc/Arc).
    // A `dyn Fn(..)` destination is Kind::Closure carrying its family name.
    const bool clos_dyn = pdyn && TypeRef(pdyn).kind() == LogosType::Kind::Closure &&
                          !std::string(TypeRef(pdyn).trait_name()).empty();
    if (!pdyn || (TypeRef(pdyn).kind() != LogosType::Kind::TraitObject &&
                  TypeRef(pdyn).kind() != LogosType::Kind::UnsizedDyn && !clos_dyn)) return;
    check_object_lifetime_bound(arg, pt, pdyn, peel);
    if (clos_dyn) return;  // no Send/Sync bits on a Closure-kind dyn
    bool need_send = TypeRef(pdyn).trait_requires_send();
    bool need_sync = TypeRef(pdyn).trait_requires_sync();
    if (!need_send && !need_sync) return;
    // The source's structural-Send/Sync check walks to the erased concrete type
    // (`&Foo → Foo`, `Box<Foo> → Foo`) or `expr_type(arg)` itself if already concrete.
    TypeRef src = expr_type(arg);
    if (!src) return;
    TypeRef src_pointee = peel(src);
    // Only enforce when the source is a CONCRETE type being unsize-erased into
    // a dyn target — Struct / ZonedStruct / Enum. TraitObject-to-TraitObject
    // coercion (e.g. dyn-upcast or identity) carries its own bound info on the
    // source side; bound preservation across that path is handled by
    // types_compatible's TypeUID-based equality (the const_val bits make
    // `&dyn T` and `&dyn T + Send` interrn as distinct types). TypeVar
    // sources are deferred to the mono-instantiation site.
    auto spk = TypeRef(src_pointee).kind();
    if (spk != LogosType::Kind::Struct &&
        spk != LogosType::Kind::ZonedStruct &&
        spk != LogosType::Kind::Enum)
        return;
    if (need_send) {
        StrSet seen;
        if (!is_auto_trait_satisfied(src_pointee, "Send", seen))
            error(std::format(
                "coercion to `&dyn {} + Send`: source type `{}` does not "
                "satisfy `Send`",
                std::string(TypeRef(pdyn).trait_name()), type_str(src_pointee)));
    }
    if (need_sync) {
        StrSet seen;
        if (!is_auto_trait_satisfied(src_pointee, "Sync", seen))
            error(std::format(
                "coercion to `&dyn {} + Sync`: source type `{}` does not "
                "satisfy `Sync`",
                std::string(TypeRef(pdyn).trait_name()), type_str(src_pointee)));
    }
}

// THE OBJECT-LIFETIME BOUND (Rust: coercing `Src` into `dyn Tr + 'r` requires
// `Src: 'r`; an OWNED `Box<dyn Tr>` with no bound defaults 'r = 'static, a
// BORROWED `&dyn Tr` with an elided slot has no obligation). `Src: 'r` is read
// off the source type's components: region slots, a struct's lifetime args (an
// elided arg of a lifetime-generic struct is a fresh region), a type parameter
// through its declared `T: 'x` bounds, a projection through its base, a closure
// through its literal's captures. The source is the operand under the Cast that
// coerce_arg_to_dyn already wrapped around it.
void SemaChecker::check_object_lifetime_bound(lir_view::ExprRef arg, TypeRef pt, TypeRef pdyn,
                                              const std::function<TypeRef(TypeRef)>& peel) {
    using K = LogosType::Kind;
    bool via_ref = false;
    for (TypeRef q = pt; q;) {
        auto k = TypeRef(q).kind();
        if ((k == K::Ref || k == K::MutRef || k == K::Ptr) && TypeRef(q).pointee()) { via_ref = true; q = TypeRef(q).pointee(); continue; }
        break;
    }
    bool borrowed = via_ref;
    if (TypeRef(pdyn).kind() == K::TraitObject && TypeRef(pdyn).trait_owning_kind() == TypeRef::OwningKind::Borrow) borrowed = true;
    if (TypeRef(pdyn).kind() == K::Closure && !TypeRef(pdyn).const_val()) borrowed = true;
    std::string slot(TypeRef(pdyn).lifetime());
    std::string R = !slot.empty() ? outlives_norm(slot) : (borrowed ? std::string() : std::string("'static"));
    if (R.empty() || R == "'_") return;
    lir_view::ExprRef er = expr_ref_of(arg);
    for (int casts = 0; casts < 2 && er.kind() == lir_schema::expr::Code::Cast; ++casts) {
        auto op = lir_view::ECastView{er}.operand();
        if (!op) break;
        er = op;
    }
    TypeRef src0 = expr_type(er);
    TypeRef sp = src0 ? peel(src0) : TypeRef();
    if (!sp || TypeRef(sp).kind() == K::TraitObject || TypeRef(sp).kind() == K::UnsizedDyn) return;
    // The literal behind the source: the literal itself, a binding whose RHS
    // was one, or the one argument of the `Box::new(..)` that produced this
    // `Box<closure>` (the arg's type is the literal's type, pointer for pointer).
    std::string src_closure_id;
    for (lir_view::ExprRef ce = er; ce;) {
        auto ck = ce.kind();
        if (ck == lir_schema::expr::Code::ClosureBox) {
            src_closure_id = std::string(lir_view::EClosureBoxView{ce}.closure_id()); break;
        }
        if (ck == lir_schema::expr::Code::VarRef) {
            if (auto* vi = lookup_var_info(lir_view::EVarRefView{ce}.name())) src_closure_id = vi->closure_id;
            break;
        }
        if (ck == lir_schema::expr::Code::Cast) { ce = lir_view::ECastView{ce}.operand(); continue; }
        if (ck == lir_schema::expr::Code::Call && TypeRef(sp).kind() == K::Closure &&
            std::string(TypeRef(sp).trait_name()).empty()) {
            lir_view::ExprRef only; int n = 0;
            lir_view::ECallView{ce}.each_arg([&](lir_view::ExprRef a) { only = a; ++n; });
            if (n == 1 && only && expr_type(only) == sp) { ce = only; continue; }
        }
        break;
    }
    auto adj = outlives_adj(current_outlives_);
    auto r_ok = [&](std::string_view r) -> bool {
        if (r.empty() || r == "'_" || r == "_") return false;
        if (outlives_is_static(r)) return true;
        return outlives(r, R, adj, /*permissive_empty=*/false);
    };
    auto tv_ok = [&](std::string_view tvn) -> bool {
        auto it = current_type_lt_outlives_.find(std::string(tvn));
        if (it == current_type_lt_outlives_.end()) return false;
        for (auto& b : it->second) if (r_ok(b)) return true;
        return false;
    };
    std::vector<std::string> bad;
    std::function<void(TypeRef, const std::string&, int)> walk = [&](TypeRef t, const std::string& cid, int d) {
        if (!t || d > 6) return;
        auto k = TypeRef(t).kind();
        switch (k) {
        case K::Ref: case K::MutRef: case K::Slice: case K::DstRef: {
            if (k == K::Slice && TypeRef(t).owning_slice()) { walk(TypeRef(t).elem(), "", d + 1); return; }
            std::string lt(TypeRef(t).lifetime());
            if (!r_ok(lt)) bad.push_back(lt.empty() ? std::string("'_") : lt);
            if (k == K::Ref || k == K::MutRef) walk(TypeRef(t).pointee(), "", d + 1);
            else if (k == K::Slice) walk(TypeRef(t).elem(), "", d + 1);
            return;
        }
        case K::TraitObject: {
            std::string lt(TypeRef(t).lifetime());
            if (lt.empty() && TypeRef(t).owning_trait_object()) return;  // an owned object with no bound is 'static
            if (!r_ok(lt)) bad.push_back(lt.empty() ? std::string("'_") : lt);
            return;
        }
        case K::Struct: case K::ZonedStruct: case K::Enum:
            for (auto& la_ : TypeRef(t).lifetime_args()) if (!r_ok(la_)) bad.push_back(la_.empty() ? std::string("'_") : la_);
            if (decl_lt_arity_(t) > TypeRef(t).lifetime_args().size())
                bad.push_back("'_ (elided lifetime argument of " + type_str(t) + ")");
            for (auto a : TypeRef(t).type_args()) walk(a, "", d + 1);
            return;
        case K::Tuple: for (auto e : TypeRef(t).tuple_elems()) walk(e, "", d + 1); return;
        case K::Array: walk(TypeRef(t).elem(), "", d + 1); return;
        case K::TypeVar: {
            std::string tvn(TypeRef(t).type_var_name());
            if (!tv_ok(tvn)) bad.push_back(tvn + " (no bound reaching " + R + ")");
            return;
        }
        case K::AssocType: {
            // RFC 1214: `T: 'r` discharges `T::Item: 'r`. A declared projection
            // bound (`where T::Item: 'r`) is not parseable today.
            TypeRef base = TypeRef(t).assoc_base();
            if (base && TypeRef(base).kind() == K::TypeVar) {
                if (!tv_ok(TypeRef(base).type_var_name()))
                    bad.push_back(type_str(t) + " (no bound on " + std::string(TypeRef(base).type_var_name()) + " reaching " + R + ")");
            }
            return;
        }
        case K::Closure: {
            if (cid.empty()) return;  // a closure type with no literal in view (a `dyn Fn` value): nothing to read
            auto ce = closure_caps_by_id_.find(cid);
            if (ce == closure_caps_by_id_.end()) return;
            for (auto& [ct, ccid] : ce->second) walk(ct, ccid, d + 1);
            return;
        }
        default: return;
        }
    };
    walk(sp, src_closure_id, 0);
    if (!bad.empty())
        error(std::format("coercion to `dyn {}` requires `{}: {}` — lifetime `{}` may not live long enough (object lifetime bound)",
                          std::string(TypeRef(pdyn).trait_name()), type_str(sp), R, bad.front()));
}

bool SemaChecker::try_implicit_reborrow_mut(lir::LExprPtr& arg, TypeRef pt,
                                              bool allow_downgrade) {
    // Rust auto-reborrows `&mut T` at call/method coercion sites where the
    // formal expects either `&mut T` (mut reborrow) or `&T` (downgrade
    // reborrow as shared). The wrapped expression — AddrOfTemp(Deref(r)) —
    // routes through borrow_check's AddrOfTemp(Deref(VarRef ref-typed))
    // handler, which registers a borrow on r rather than consuming it.
    // Without this, every fn call passing a `&mut T` arg would move it,
    // making Rust-idiom (`f.write_str(x); v.fmt(f);`) reject.
    //
    // The reborrow is purely STRUCTURAL — the wrapped expression has the
    // SAME type as `arg`, so we don't require `types_compatible(arg, pt)`:
    // the existing argument type-check will run after this and flag any
    // genuine mismatch. We only need to know the FORMAL is ref-shaped
    // (mut or shared) so the reborrow makes semantic sense; in particular
    // a generic `pt = &mut Self` (TypeVar pointee) is fine — the reborrow
    // doesn't reify Self.
    if (!arg || !pt) return false;
    if (TypeRef(expr_type(arg)).kind() != LogosType::Kind::MutRef) return false;
    auto pkind = TypeRef(pt).kind();
    bool dest_mut;
    if (pkind == LogosType::Kind::MutRef) dest_mut = true;
    else if (pkind == LogosType::Kind::Ref) {
        // Downgrading reborrow `&mut → &` is correct for fn-arg coercion
        // (Rust auto-downgrades), but at the method-receiver position the
        // formal `&Self` carries Self = `&mut X` for an `impl Trait for &mut
        // X` (Self IS the ref), and downgrading would dispatch through the
        // wrong impl key. Caller passes allow_downgrade=false there.
        if (!allow_downgrade) return false;
        dest_mut = false;
    }
    else if (pkind == LogosType::Kind::Ptr) dest_mut = TypeRef(pt).mut_ptr();
    else return false;
    TypeRef arg_pointee = TypeRef(expr_type(arg)).pointee();
    if (!arg_pointee) return false;
    // Reborrow only applies to a PLACE holding a `&mut T` — a binding (VarRef)
    // or a field access yielding `&mut T`. Bare `&mut x` / `&mut p.f` is
    // already a FRESH borrow expression (AddrOf/AddrOfTemp) — wrapping it in
    // a reborrow shape would hide it from borrow_check's normal recording
    // path, silently dropping the borrow.
    // ⚠ TupleIndex ADDED, AND THIS SITE IS DELIBERATELY *NOT* DELEGATED TO
    // lir_view::is_place_expr, unlike the three match-scrutinee lists.
    // The shape this produces — AddrOfTemp(Deref(<arg>)) — has exactly one
    // recogniser: lir_view::is_reborrow_shape, which matches ONLY
    // AddrOfTemp(Deref(VarRef)), and borrow_check's reborrow handler is keyed
    // on the same. Widening the PRODUCER past what the RECOGNISER accepts
    // manufactures expressions borrow_check does not see as reborrows — which
    // is the failure this function's own comment above already names, and it is
    // PERMISSIVE, so a green corpus cannot see it either.
    // FieldRead is already here and already exercises the non-VarRef path, so
    // TupleIndex rides a proven route: `t.0` is a field whose name is its
    // index. Deref and SliceIndex wait for is_reborrow_shape to be widened
    // first — that is its own arc, not a line in this one.
    // ⚠ Deref ADDED, AND THE BLOCK THAT KEPT IT OUT IS NOW LIFTED. It was held
    // back because the only recogniser of the shape this produces matched
    // `AddrOfTemp(Deref(VarRef))` alone, so `f(*p)` would have been wrapped into
    // `AddrOfTemp(Deref(Deref(p)))` and recognised by nothing. borrow_check's
    // AddrOfTemp arm now PEELS A DEREF CHAIN instead of matching one deref, so
    // the wrap is recorded. MEASURED: `f(*p, *p)` with `p: &mut &mut i64`
    // compiled rc 0 — two live `&mut i64` onto one storage, E0499 — while its
    // one-property twin `f(p, p)` refused.
    // ⚠ THE REMAINING CONSUMER IS CODEGEN: mlir_gen_dyn's dyn_storage_ptr still
    // unwraps exactly one deref, and a wrap it fails to recognise is read as a
    // by-value fat pair — a SEGFAULT, not a refusal. That shape needs a
    // `& &dyn Tr` receiver, which the type grammar does not parse today
    // (measured: "syntax error near '&'"), so it is unreachable rather than
    // handled. If reference-to-reference types ever parse, dyn_storage_ptr must
    // peel the chain before this line is safe.
    // SliceIndex still waits: it has no demonstrated live hole yet, and one
    // spelling at a time is how each of these is measured.
    auto k = expr_ref_of(arg).kind();
    if (k != lir_schema::expr::Code::VarRef &&
        k != lir_schema::expr::Code::FieldRead &&
        k != lir_schema::expr::Code::TupleIndex &&
        k != lir_schema::expr::Code::Deref &&
        k != lir_schema::expr::Code::IndexRead)
        return false;
    // The reborrow carries the reborrowed reference's region (2026-09-02s).
    std::string arg_region(TypeRef(expr_type(arg)).lifetime());
    auto deref = builder().deref(std::move(arg), arg_pointee);
    arg = builder().addr_of_temp(std::move(deref), /*is_mut=*/dest_mut,
                                  make_ref(dest_mut, arg_pointee, arg_region));
    return true;
}

bool SemaChecker::coerce_dyn_upcast(lir::LExprPtr& arg, TypeRef pt) {
    if (!arg || !pt) return false;
    TypeRef at(expr_type(arg));
    if (TypeRef(at).kind() == LogosType::Kind::Error) return false;
    // Source must be a trait object (bare or behind a ref).
    TypeRef src_to = at;
    if ((at.kind() == LogosType::Kind::Ref || at.kind() == LogosType::Kind::MutRef) &&
        at.pointee()) src_to = at.pointee();
    if (TypeRef(src_to).kind() != LogosType::Kind::TraitObject) return false;
    if (types_compatible(at, pt)) return false;  // identical dyn — no upcast
    // Peel param to its bare TraitObject.
    TypeRef pdyn = pt;
    if ((TypeRef(pt).kind() == LogosType::Kind::Ref ||
         TypeRef(pt).kind() == LogosType::Kind::MutRef) && TypeRef(pt).pointee())
        pdyn = TypeRef(pt).pointee();
    if (TypeRef(pdyn).kind() != LogosType::Kind::TraitObject) return false;
    std::string sub(TypeRef(src_to).trait_name());
    std::string super(TypeRef(pdyn).trait_name());
    if (sub.empty() || super.empty()) return false;
    // Super must be a (transitive) supertrait of Sub. Works for bare-`dyn`
    // sources too (ref_arg_satisfies_dyn requires a ref source, so reachability
    // is checked here directly).
    logos::compiler::StrSet seen;
    std::function<bool(const std::string&)> reaches =
        [&](const std::string& tn) -> bool {
            if (!seen.insert(tn).second) return false;
            if (tn == super) return true;
            auto it = traits_.find(tn);
            if (it == traits_.end()) return false;
            for (auto& s : it->second.supertraits)
                if (reaches(s.trait_name)) return true;
            return false;
        };
    if (sub == super || !reaches(sub)) return false;
    mark_coercion_source_moved(arg);   // see mark_coercion_source_moved (R2)
    arg = builder().cast(std::move(arg), pt);
    return true;
}

bool SemaChecker::coerce_arg_to_dyn(lir::LExprPtr& arg, TypeRef pt) {
    if (!arg || !pt) return false;
    if (TypeRef(expr_type(arg)).kind() == LogosType::Kind::Error) return false;
    if (types_compatible(expr_type(arg), pt)) return false;  // already fits
    // Implicit CoerceUnsized for a smart-pointer/wrapper struct param
    // (`Rc<A>` → `Rc<dyn Tr>`): rebuild unsizing the inner field. Mirrors the
    // explicit `as` path; closes GAP-C for the flipped struct repr.
    if (try_struct_unsize_coerce(arg, pt)) return true;
    // Peel a `&dyn` / `&mut dyn` param to the bare TraitObject.
    TypeRef pdyn = pt;
    auto pk = TypeRef(pt).kind();
    if ((pk == LogosType::Kind::Ref || pk == LogosType::Kind::MutRef) &&
        TypeRef(pt).pointee())
        pdyn = TypeRef(pt).pointee();
    if (TypeRef(pdyn).kind() != LogosType::Kind::TraitObject) return false;
    if (!ref_arg_satisfies_dyn(expr_type(arg), pdyn)) return false;
    mark_coercion_source_moved(arg);   // see mark_coercion_source_moved (R3)
    arg = builder().cast(std::move(arg), pt);
    return true;
}

// ── #68 CLASS: an aggregate literal's SLOT TYPES have no second source ─────
// A struct FIELD's type comes from the struct declaration, so mlir-gen can
// unsize `&Concrete` into a `&dyn Trait` field in every context and does
// (gen_struct_lit — verified still green here in every context probed). A TUPLE
// is structural (its type IS its elements' types) and an ARRAY literal's element
// type is read off the LITERAL NODE too (gen_arr_lit's `logos_elem`,
// tuple_llvm_type) — so `(&a, 7)` is TYPED `(&Sq, i64)`, a 16-byte aggregate,
// and every consumer that expects `(&dyn Shape, i64)` reads 24.
// The only site that used to repair it was the `let`-annotation `retype_expr`
// in lower_let, which is why the defect was invisible under an annotated `let`
// and fatal everywhere else (MEASURED, one runtime value each: call arg 139,
// nested tuple 2, fn return 1, match arm 1, generic arg 1, struct field 1,
// assignment 1, array-of-tuples 1; `--emit-mlir` showed the caller building
// `struct<(ptr, i64)>` for a callee reading `struct<(struct<(ptr, ptr)>, i64)>`).
//
// So the repair is the SAME operation lower_let already performed, moved to the
// one judgment that every position goes through (`expect_type` →
// `coerce_arg_to_param`): stamp the expected tuple type onto the literal, IN
// PLACE. mlir-gen's ETupleLit arm then does the unsize from the slot type, the
// way the struct and array arms always did. In place matters: a match ARM or an
// array ELEMENT is a sub-expression of a node this function is handed, and a
// rebuild would have to re-emit the arm/pattern mirrors — the walk below
// retypes the literal where it sits instead.

// Does slot type `tgt` accept element type `at` only by an UNSIZE to `&dyn`?
// `&dyn Trait` IS Kind::TraitObject (`Ref<UnsizedDyn<Trait>>` is canonicalised
// to it at resolve time), so the slot is asked for DIRECTLY, never peeled.
// ⚠ THE `*const dyn` CLAIM THIS COMMENT USED TO CARRY WAS WRONG TWICE OVER, and
// it is corrected here rather than repeated. It said the single line above
// "keeps `*const dyn Trait` out" because a raw dyn pointer "deliberately keeps
// 8-byte handle semantics". Both halves are refuted by measurement:
//   • the WIDTH: `sizeof::<*const dyn Shape>() == 16`
//     (/home/logos/sandbox/aggunsize/z_rawdyn.logos), and coerce_to_dyn in
//     mlir_gen_dyn.cpp states the model outright — "`&dyn`/`*dyn`/`Box<dyn>` are
//     all uniform 16-byte fat", the thin-handle path having been REMOVED as
//     provably unreachable across the whole corpus.
//   • the EXCLUSION: there is none. A `*const dyn Trait` SLOT is canonicalised
//     to Kind::TraitObject exactly like `&dyn Trait`, so it is asked and
//     answered here — MEASURED, `fn take(t: (*const dyn Shape, i64))` fed
//     `let p: *const Sq = &a; let t = (p, 7i64); take(t)` is refused with
//     "aggregate slot `.0`: expected &dyn Shape, got *const Sq", and the same
//     slot's `sizeof::<(*const dyn Shape, i64)>()` is 24, not 16
//     (z_rawdyn_tuple.logos / z_rawdyn_cast.logos).
// MEASURED: the peel-Ref-first spelling — the shape
// mlir-gen's arm uses — matched NOTHING here (`slot target=&dyn Shape … tk=28`).
bool SemaChecker::aggregate_slot_needs_unsize(TypeRef at, TypeRef tgt) {
    if (!at || !tgt) return false;
    if (TypeRef(tgt).kind() != LogosType::Kind::TraitObject) return false;
    if (at.kind() == LogosType::Kind::Error) return false;
    // ── #95/M2: AN OWNING `Box<dyn Trait>` SLOT ───────────────────────────────
    // The first cut of #95 EXCLUDED the owning form from both halves and
    // disclosed it as unmeasured. Measured, it was a crash, not a narrowing:
    // `(Box<Sq>, i64)` against `(Box<dyn Shape>, i64)` compiled and SIGSEGVed in
    // the tuple, the array and the struct-field shapes
    // (/home/logos/sandbox/vfy95/h/h0{1,2,8}*.logos, rc 139 each).
    //
    // AND THE DECISION IS SETTLED BY MEASUREMENT, not by symmetry-of-argument:
    // an ANNOTATED tuple literal — `let t: (Box<dyn Shape>, i64) =
    // (Box::new(Sq{..}), 7i64)` — ALREADY LOWERS CORRECTLY (rc 42, probe p/b1),
    // because mlir-gen's tuple arm reads the slot type off the literal node and
    // unsizes the owning pointer there exactly as it does the borrowed one. So
    // the fat half is not missing at codegen; it is missing at SEMA, which never
    // stamped the literal because this predicate demanded a `&`/`&mut` source.
    // The owning source is the Box STRUCT (`Box<Sq>` is Kind::Struct; only
    // `Box<dyn T>` is a Kind::TraitObject with an owning kind), so it is asked
    // for here in its own arm, and the impl/auto-bound question is delegated —
    // unchanged — by asking it about a BORROW of the payload: what may be erased
    // is a property of the payload type, and only the release semantics differ
    // between `&dyn` and `Box<dyn>`. The HOISTED owning form has no literal to
    // stamp and is refused by find_uncoerced_aggregate_slot, same as borrowed.
    if (TypeRef(tgt).owning_trait_object()) {
        if (!is_stdlib_box(at)) return false;   // already fat, or not a Box
        auto ta = at.type_args();
        if (ta.size() != 1 || !ta[0]) return false;
        if (TypeRef(ta[0]).kind() == LogosType::Kind::TraitObject) return false;
        return ref_arg_satisfies_dyn(make_ref(false, ta[0]), tgt);
    }
    // Already the fat pair (a `&a as &dyn Shape` element, or a `&dyn` binding):
    // NOT an unsize, and must not be counted as one — no double coercion.
    if (at.kind() != LogosType::Kind::Ref && at.kind() != LogosType::Kind::MutRef)
        return false;
    return ref_arg_satisfies_dyn(at, tgt);
}

bool SemaChecker::retype_aggregate_lit_to(lir_view::ExprRef er, TypeRef target) {
    if (!er || !target) return false;
    TypeRef et(er.type(cur_prog_->type_pool.impl()));
    if (!et) return false;
    // ── the WRAPPERS: the literal is a sub-expression, retype it where it is ──
    switch (er.kind()) {
    case lir_schema::expr::Code::MatchExpr: {
        bool any = false;
        lir_view::EMatchExprView{er}.each_arm([&](lir_view::EMatchArmRef a) {
            if (auto v = a.value()) any = retype_aggregate_lit_to(v, target) || any;
        });
        if (any) builder().retype_expr(er, target);
        return any;
    }
    case lir_schema::expr::Code::IfExpr: {
        lir_view::EIfExprView v{er};
        bool any = false;
        if (v.then_val()) any = retype_aggregate_lit_to(v.then_val(), target) || any;
        if (v.else_val()) any = retype_aggregate_lit_to(v.else_val(), target) || any;
        if (any) builder().retype_expr(er, target);
        return any;
    }
    case lir_schema::expr::Code::BlockExpr: {
        lir_view::EBlockExprView v{er};
        bool any = v.result() && retype_aggregate_lit_to(v.result(), target);
        if (any) builder().retype_expr(er, target);
        return any;
    }
    case lir_schema::expr::Code::EnumLit:
    case lir_schema::expr::Code::EnumLitData:
        // #95/M3 — AN ENUM LITERAL IS A COERCION SITE TOO. `take(Some(&a))`
        // against `fn take(o: Option<&dyn Shape>)` used to COMPILE AND RETURN
        // THE WRONG ANSWER (rc 1, /home/logos/sandbox/vfy95/h2/g01_option_dyn):
        // the literal was typed `Option<&Sq>` from its payload and nothing ever
        // re-asked. mlir-gen's EnumLitData arm already unsizes a payload whose
        // DECLARED slot type is a TraitObject (it reads the variant's payload
        // types out of the instance named by the node's TYPE, then calls
        // coerce_value_to_dyn_if_needed) — which is why the annotated spelling
        // `let o: Option<&dyn Shape> = Some(&a)` was correct all along. So the
        // repair is the same one the tuple/array arms get: stamp the expected
        // instance onto the literal node, and let the slot rule below decide
        // whether that is legitimate.
    case lir_schema::expr::Code::TupleLit:
    case lir_schema::expr::Code::ArrLit:
        // A WRAPPER is never short-circuited on `et == target`: a match whose
        // arms are aggregate literals gets its own type from arm 0 and can
        // already READ as the dyn type while every arm still carries the thin
        // one (MEASURED: ar_match, `[&dyn Shape; 2]` outside, `[&Sq; 2]` in the
        // arms, SIGSEGV). Only a LITERAL that already IS the target is done.
        if (et == target) return false;
        break;
    default: return false;
    }
    // ── the LITERAL ───────────────────────────────────────────────────────────
    // One slot: unsize / nested stamp / already fits / REFUSE. Shared by both
    // literal shapes so the tuple and array arms cannot drift apart again.
    auto slot = [&](lir_view::ExprRef el, TypeRef ce, TypeRef te,
                    bool& has_unsize, uint64_t idx) -> bool {
        if (!ce || !te) return false;
        if (aggregate_slot_needs_unsize(ce, te)) { has_unsize = true; return true; }
        if (el && retype_aggregate_lit_to(el, te)) { has_unsize = true; return true; }
        // The PERMISSIVE twin, found by this round's own probe. A `&dyn Trait`
        // slot fed a `&Concrete` that does NOT implement the trait is accepted
        // by types_compatible (its Struct → TraitObject branch is a blanket
        // accept, "impl check deferred to codegen") — and in the AGGREGATE case
        // codegen never gets to run its check, because without a stamp nothing
        // ever attempts the coercion: `(&a, 7i64)` against `(&dyn Other, i64)`
        // with `Sq: !Other` wrote an object file, while the plain arg spelling
        // `take(&a)` is refused with "no vtable for 'Sq' as '&dyn Other'".
        // Refuse HERE, and only for a CONCRETE pointee — a TypeVar pointee is
        // mono's judgment (ref_arg_satisfies_dyn answers it from the bound set,
        // which is incomplete before substitution) and is left alone.
        // #95/M2 — and the OWNING spelling of the same hole. `Box<Sq>` fed to a
        // `Box<dyn Other>` slot with `Sq: !Other` reaches here with a Struct
        // source, not a Ref one; without this arm it fell straight through to
        // types_compatible's blanket accept and wrote an object file whose
        // vtable half is uninitialised. The erased-type question is asked about
        // the payload, exactly as aggregate_slot_needs_unsize asks it.
        TypeRef owning_payload{nullptr};
        if (TypeRef(te).kind() == LogosType::Kind::TraitObject &&
            TypeRef(te).owning_trait_object() && is_stdlib_box(ce)) {
            auto ta = ce.type_args();
            if (ta.size() == 1 && ta[0]) {
                auto pk = TypeRef(ta[0]).kind();
                if (pk != LogosType::Kind::TypeVar &&
                    pk != LogosType::Kind::AssocType &&
                    pk != LogosType::Kind::TraitObject &&
                    pk != LogosType::Kind::Error)
                    owning_payload = ta[0];
            }
        }
        if (owning_payload) {
            auto [es, gs] = type_str_pair(te, ce);
            // ⚠ NOT SPELLED AS THE MISMATCH VERDICT, and that is the point.
            // `expected {}, got {}` is expect_type's monopoly
            // (scripts/lint-mismatch-monopoly.sh), and this is a DIFFERENT
            // verdict: the types are not merely unequal, the element cannot be
            // unsized here because the trait is not implemented. Re-spelling it
            // as a mismatch would have made the lint count three emitters of
            // one verdict — which is exactly the sieve of per-site special
            // cases that lint exists to stop.
            error(std::format("aggregate element {}: slot type {} needs an "
                              "unsize from {}, but the type does not implement "
                              "the trait, so the element's vtable half would be "
                              "uninitialised",
                              idx, es, gs));
            return false;
        }
        if (TypeRef(te).kind() == LogosType::Kind::TraitObject &&
            (ce.kind() == LogosType::Kind::Ref ||
             ce.kind() == LogosType::Kind::MutRef) &&
            ce.pointee() &&
            TypeRef(ce.pointee()).kind() != LogosType::Kind::TypeVar &&
            TypeRef(ce.pointee()).kind() != LogosType::Kind::AssocType &&
            TypeRef(ce.pointee()).kind() != LogosType::Kind::TraitObject &&
            TypeRef(ce.pointee()).kind() != LogosType::Kind::Error) {
            auto [es, gs] = type_str_pair(te, ce);
            // ⚠ NOT SPELLED AS THE MISMATCH VERDICT, and that is the point.
            // `expected {}, got {}` is expect_type's monopoly
            // (scripts/lint-mismatch-monopoly.sh), and this is a DIFFERENT
            // verdict: the types are not merely unequal, the element cannot be
            // unsized here because the trait is not implemented. Re-spelling it
            // as a mismatch would have made the lint count three emitters of
            // one verdict — which is exactly the sieve of per-site special
            // cases that lint exists to stop.
            error(std::format("aggregate element {}: slot type {} needs an "
                              "unsize from {}, but the type does not implement "
                              "the trait, so the element's vtable half would be "
                              "uninitialised",
                              idx, es, gs));
            return false;
        }
        return types_compatible(ce, te);
    };
    // NARROW ON PURPOSE. The stamp only happens when at least one slot is a
    // VALIDATED `&Concrete` → `&dyn Trait` unsize (directly, or inside a nested
    // literal), and every other slot already fits. Anything else leaves the
    // literal completely alone and the ordinary mismatch diagnostic downstream
    // still fires.
    bool has_unsize = false;
    if (er.kind() == lir_schema::expr::Code::EnumLit ||
        er.kind() == lir_schema::expr::Code::EnumLitData) {
        // The enum instance's payload slot types come from the VARIANT
        // declaration substituted with the TARGET's type-args — the same
        // projection retype_enum_lit_recursive does, and the same one mlir-gen
        // will do from the stamped node. A payload-less variant (`None`) has no
        // slot to unsize, so it never stamps here: `None` against
        // `Option<&dyn Shape>` is a bare instance question, answered upstream.
        if (TypeRef(target).kind() != LogosType::Kind::Enum) return false;
        if (et.kind() != LogosType::Kind::Enum) return false;
        if (et.enum_name() != TypeRef(target).enum_name()) return false;
        if (et.pkg_name() != TypeRef(target).pkg_name()) return false;
        if (er.kind() != lir_schema::expr::Code::EnumLitData) return false;
        lir_view::EEnumLitDataView v{er};
        auto [pkg, esi] = find_enum_by_name(std::string(v.enum_name()));
        (void)pkg;
        if (!esi) return false;
        const SemaVariantInfo* vinfo = nullptr;
        std::string vn(v.variant());
        for (auto& vv : esi->variants) if (vv.name == vn) { vinfo = &vv; break; }
        if (!vinfo || vinfo->payload_types.empty()) return false;
        SemaSubst subst;
        auto cta = TypeRef(target).type_args();
        if (cta.size() != esi->type_params.size()) return false;
        for (size_t i = 0; i < esi->type_params.size(); ++i)
            if (cta[i]) subst[esi->type_params[i].name] = cta[i];
        std::vector<lir_view::ExprRef> pl;
        v.each_payload([&](lir_view::ExprRef pe){ pl.push_back(pe); });
        if (pl.size() != vinfo->payload_types.size()) return false;
        for (size_t i = 0; i < pl.size(); ++i) {
            TypeRef tt = vinfo->payload_types[i];
            if (!tt) return false;
            if (!subst.empty()) tt = subst_type_sema(tt, subst);
            if (!pl[i]) return false;
            if (!slot(pl[i], TypeRef(pl[i].type(cur_prog_->type_pool.impl())),
                      TypeRef(tt), has_unsize, i))
                return false;
        }
        if (!has_unsize) return false;
        builder().retype_expr(er, target);
        // Nested enum payloads (`Some(Some(&a))`) need the SAME projection one
        // level down, and that walk already exists.
        retype_enum_lit_recursive(er, target);
        return true;
    }
    if (er.kind() == lir_schema::expr::Code::TupleLit) {
        if (TypeRef(target).kind() != LogosType::Kind::Tuple) return false;
        if (et.kind() != LogosType::Kind::Tuple) return false;
        const auto& tgt_elems = TypeRef(target).tuple_elems();
        const auto& cur_elems = et.tuple_elems();
        if (tgt_elems.size() != cur_elems.size()) return false;
        lir_view::ETupleLitView v{er};
        if (v.count() != tgt_elems.size()) return false;
        for (uint64_t i = 0; i < v.count(); ++i)
            if (!slot(v.elem(i), TypeRef(cur_elems[i]), TypeRef(tgt_elems[i]),
                      has_unsize, i))
                return false;
    } else {
        if (TypeRef(target).kind() != LogosType::Kind::Array) return false;
        if (et.kind() != LogosType::Kind::Array) return false;
        TypeRef te = TypeRef(target).elem();
        if (!te) return false;
        lir_view::EArrLitView v{er};
        // A stamp must never change the LENGTH — `[x; 2]` is not an `[T; 3]`.
        if (TypeRef(target).arr_size() != v.count()) return false;
        for (uint64_t i = 0; i < v.count(); ++i) {
            auto el = v.elem(i);
            if (!el) return false;
            if (!slot(el, TypeRef(el.type(cur_prog_->type_pool.impl())), te,
                      has_unsize, i))
                return false;
        }
    }
    if (!has_unsize) return false;
    builder().retype_expr(er, target);
    return true;
}

lir::LExprPtr SemaChecker::lower_typaram_static_method(
        const std::string& cname, const std::string& mname,
        std::vector<TypeRef> explicit_targs,
        std::vector<lir::LExprPtr> arg_exprs) {
    auto bit = current_type_bounds_.find(cname);
    if (bit == current_type_bounds_.end()) return nullptr;
    // Walk the type-param's bounds (+ supertraits) for a STATIC method `mname`
    // (first param isn't `Self`).
    const SemaTraitMethodInfo* m = nullptr;
    bool prov_trait_has_targs = false;
    logos::compiler::StrSet seen;
    std::function<void(const std::string&)> walk = [&](const std::string& tn) {
        if (m || !seen.insert(tn).second) return;
        auto it = find_trait_iter_scoped(tn);
        if (it == traits_.end()) return;
        for (auto& mm : it->second.methods) {
            if (mm.name != mname) continue;
            bool is_static = mm.param_types.empty() ||
                !(mm.param_types[0] &&
                  TypeRef(mm.param_types[0]).kind() == LogosType::Kind::TypeVar &&
                  TypeRef(mm.param_types[0]).type_var_name() == "Self");
            if (is_static) {
                m = &mm;
                prov_trait_has_targs = !it->second.type_params.empty();
                return;
            }
        }
        for (auto& s : it->second.supertraits) walk(s.trait_name);
    };
    for (auto& b : bit->second) { walk(b.trait_name); if (m) break; }
    if (!m) return nullptr;
    // Multi-param-trait static dispatch (`S: Collect<A>` / `Sum<Item>`, trait WITH
    // type-args) is disambiguated by mono via the arg-type suffix and needs the
    // type-args left EMPTY at sema — passing the method's own type-args here
    // breaks that retarget (Gap A'). Defer those to the caller's old path; the
    // general resolver handles only single-dispatch traits (Self-keyed, no trait
    // type-args — e.g. `Zone`).
    if (prov_trait_has_targs) return nullptr;
    // GENERALIZATION: synthesize a SemaFuncInfo from the trait method (Self → the
    // type-param Z), then route through the SAME resolver every other generic call
    // uses — finish_generic_call. It does turbofish + arg-inference + return-hint
    // inference + fn-family-bound propagation uniformly, then emits a call to the
    // abstract `Z__method` symbol (passthrough, no re-mangling) that mono retargets
    // to `Concrete__method::<..>`. Replaces the hand-rolled partial handling.
    SemaSubst self_subst;
    self_subst["Self"] = current_type_params_.count(cname)
        ? current_type_params_[cname] : make_typevar(cname);
    SemaFuncInfo synth;
    synth.type_params = m->type_params;
    synth.param_types.reserve(m->param_types.size());
    for (auto& pt : m->param_types)
        synth.param_types.push_back(pt ? subst_type_sema(pt, self_subst) : pt);
    synth.ret_type = m->ret_type ? subst_type_sema(m->ret_type, self_subst) : void_t();
    synth.is_unsafe = m->is_unsafe;
    synth.impl_target_pattern = nullptr;
    synth.body_always_diverges = false;
    // The composed base below is `<cname>__<mname>`; carry both parts so no
    // consumer has to recover them by cutting at a `__`.
    synth.owner_struct = cname;
    synth.is_method    = true;
    return finish_generic_call(cname + "__" + mname, synth,
                               std::move(explicit_targs), std::move(arg_exprs));
}

lir::LExprPtr SemaChecker::lower_static_call(TinyMapView node) {
    // Phase 1B-5 parity for TYPE-side turbofish (`PkdArray::<str>::format`):
    // bare `[T]`/`str`-as-unsized/`dyn` type args are legal when the class's
    // param is `?Sized` (or a partial spec may govern). resolve_generic's
    // Sized-enforcement still rejects genuinely wrong args; without this the
    // arg canonicalised to the VALUE form (&[u8]) and the instantiation
    // family silently changed.
    struct UOkGuard {
        bool& flag; bool saved;
        UOkGuard(bool& f, bool v) : flag(f), saved(f) { flag = v; }
        ~UOkGuard() { flag = saved; }
    };
    std::optional<UOkGuard> static_call_uok_;
    {
        std::string cn0(str_of(node.get(la::RECEIVER.code)));
        auto [p0, ssi0] = find_struct_by_name(cn0);
        (void)p0;
        bool relax = false;
        if (ssi0) {
            for (auto& tp : ssi0->type_params)
                if (!tp.implicit_sized) { relax = true; break; }
            if (!relax && struct_has_specs(cn0)) relax = true;
        }
        if (relax) static_call_uok_.emplace(unsized_ok_, true);
    }

    std::string class_name(str_of(node.get(la::RECEIVER.code)));
    auto method_name = str_of(node.get(la::NAME.code));

    // T2-28 (Increment 2): a qualified `pkg.path.Type::member(args)` is parsed
    // as the qualified-CALL shape (RECEIVER = first segment, QUAL_PARTS = the
    // rest, member in CALLEE) and delegated here by lower_call once it finds no
    // free fn of that name in the package. The LAST dotted segment is the type;
    // the package prefix is dropped (type resolution searches by name). Clear
    // the package qualifier — type/method resolution + arg lowering are not
    // package-filtered (only free-fn lookups are).
    if (node.has_key(la::QUAL_PARTS)) {
        auto parts = arr_of(node.get(la::QUAL_PARTS.code));
        if (parts.size() >= 1)
            class_name = std::string(
                str_of(map_of(parts.get(parts.size() - 1)).get(la::NAME.code)));
        if (method_name.empty()) method_name = str_of(node.get(la::CALLEE.code));
        call_pkg_qualifier_.clear();
    }

    // G153-4: `Self::method()` inside an impl body — resolve `Self` to the
    // impl's concrete type name (bound in current_type_params_["Self"]) so the
    // static method resolves, exactly as if the type name were written.
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
            if ((logos::probe::on("selfve") || logos::probe::on("selfvee")) && st.kind() == LogosType::Kind::Enum) selfve_self_ = sit->second;
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
                if ((logos::probe::on("selfve") || logos::probe::on("selfvee")) && selfve_self_) {
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
