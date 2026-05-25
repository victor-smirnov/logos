// Logos project — https://github.com/victor-smirnov/logos

#include "sema_impl.hpp"

#include <cstdio>
#include <format>
#include <functional>
#include <unordered_set>

namespace logos::compiler {

namespace la = ast;
using hermes::TinyMapView;
using hermes::ArrayView;
using hermes::StringView;
using hermes::AnyVal;
using hermes::MemHolder;

// Declaration lowering methods

void SemaChecker::compute_fn_lifetime_outlives(TinyMapView node, lir::LFunction& fn) {
    std::unordered_set<std::string> fn_lts(fn.lifetime_params.begin(),
                                           fn.lifetime_params.end());
    auto fn_lifetime_known = [&](std::string_view lt) {
        if (lt.empty()) return false;
        if (lt == "'static" || lt == "static") return true;
        return fn_lts.count(std::string(lt)) > 0;
    };
    auto walk_implied = [&](TypeRef t, std::string outer,
                            std::vector<std::pair<std::string, std::string>>& out,
                            auto& recur) -> void {
        if (!t) return;
        auto kind = t.kind();
        if (kind == LogosType::Kind::Ref || kind == LogosType::Kind::MutRef) {
            std::string my_lt(t.lifetime());
            // Only emit pairs whose lifetimes are declared on the fn —
            // generic / template-internal lifetimes (from a generic
            // struct's definition) aren't fn-scope, and surfacing them
            // here would trigger spurious "undeclared lifetime" errors.
            if (!outer.empty() && !my_lt.empty() && my_lt != outer &&
                fn_lifetime_known(my_lt) && fn_lifetime_known(outer))
                out.emplace_back(my_lt, outer);
            std::string next_outer = my_lt.empty() ? outer : my_lt;
            recur(t.pointee(), next_outer, out, recur);
            return;
        }
        if (kind == LogosType::Kind::Struct || kind == LogosType::Kind::ZonedStruct ||
            kind == LogosType::Kind::Enum) {
            for (auto& lt : t.lifetime_args()) {
                std::string s(lt);
                if (!outer.empty() && !s.empty() && s != outer &&
                    fn_lifetime_known(s) && fn_lifetime_known(outer))
                    out.emplace_back(s, outer);
            }
            for (auto a : t.type_args()) recur(a, outer, out, recur);
            return;
        }
        if (kind == LogosType::Kind::Tuple) {
            for (auto e : t.tuple_elems()) recur(e, outer, out, recur);
            return;
        }
        if (kind == LogosType::Kind::Slice || kind == LogosType::Kind::Array) {
            recur(t.elem(), outer, out, recur);
            return;
        }
        if (kind == LogosType::Kind::Ptr) {
            recur(t.pointee(), outer, out, recur);
            return;
        }
    };
    fn.lifetime_outlives = read_lifetime_outlives(node);
    for (auto& p : fn.params) walk_implied(p.type, "", fn.lifetime_outlives, walk_implied);
    walk_implied(fn.ret_type, "", fn.lifetime_outlives, walk_implied);
    auto where_outlives = read_lifetime_outlives_from(node, la::WHERE.code);
    for (auto& p : where_outlives) fn.lifetime_outlives.push_back(std::move(p));
    // Merge type-outlives bounds from where clause.
    if (node.has_key(la::WHERE)) {
        AnyVal wav = node.get(la::WHERE.code);
        if (!wav.is_null()) {
            auto wmap = map_of(wav);
            if (wmap.has_key(la::ITEMS)) {
                auto witems = arr_of(wmap.get(la::ITEMS.code));
                for (uint64_t i = 0; i < witems.size(); ++i) {
                    auto witem = map_of(witems.get(i));
                    if (code_of(witem) != la::TYPE_PARAM) continue;
                    if (!witem.has_key(la::ITEMS)) continue;
                    std::string tname(str_of(witem.get(la::NAME.code)));
                    auto inner = arr_of(witem.get(la::ITEMS.code));
                    TypeParam* target = nullptr;
                    for (auto& tp : fn.type_params)
                        if (tp.name == tname) { target = &tp; break; }
                    if (!target) continue;
                    for (uint64_t j = 0; j < inner.size(); ++j) {
                        auto inode = map_of(inner.get(j));
                        if (code_of(inode) != la::LIFETIME_PARAM) continue;
                        target->lifetime_outlives.push_back(
                            std::string(str_of(inode.get(la::NAME.code))));
                    }
                }
            }
        }
    }
    // Validate declared names.
    std::unordered_set<std::string> declared(fn.lifetime_params.begin(),
                                             fn.lifetime_params.end());
    auto known = [&](std::string_view lt) {
        if (lt.empty()) return true;
        if (lt == "'static" || lt == "static") return true;
        return declared.count(std::string(lt)) > 0;
    };
    for (auto& [lng, sht] : fn.lifetime_outlives) {
        if (!known(lng))
            error(std::format("fn '{}': use of undeclared lifetime name '{}' in outlives clause",
                              fn.name, lng));
        if (!known(sht))
            error(std::format("fn '{}': use of undeclared lifetime name '{}' in outlives clause",
                              fn.name, sht));
    }
    for (auto& tp : fn.type_params) {
        for (auto& lt : tp.lifetime_outlives) {
            if (!known(lt))
                error(std::format("fn '{}': use of undeclared lifetime name '{}' in `{}: {}` bound",
                                  fn.name, lt, tp.name, lt));
        }
    }
}

lir::LFunction SemaChecker::lower_fn(TinyMapView node, std::string_view struct_ctx) {
    auto raw_name = str_of(node.get(la::NAME.code));
    // Sprint 6.3 — B-fn-08: reserve `_` for ignored-binding semantics.
    // Allowing `fn _()` would let `_(...)` be a valid call expression and
    // collide with future ignored-binding patterns.
    if (raw_name == "_") {
        error("'_' is reserved for ignored bindings; pick a different fn name");
    }
    std::string mangled = struct_ctx.empty()
        ? std::string(raw_name)
        : std::string(struct_ctx) + "__" + std::string(raw_name);

    // Trait-aware method mangling: if this method's name collided with another
    // trait's same-named method on this type, collect_fn re-keyed it under the
    // trait-qualified base `<target>__<trait>__<method>`. Switch `mangled` to
    // that base so the fi_ptr lookup and `fn.name` (and thus the emitted LIR
    // function symbol) match the qualified registration. Only fires when the
    // qualified base is actually registered, so non-colliding methods are
    // byte-identical to before.
    if (!current_impl_trait_name_.empty() && !struct_ctx.empty()) {
        // G156-1: include the impl's trait type-args in the qualified base
        // (`X__Trait$u64__m`) so two `impl Trait<A> for X` at distinct A get
        // distinct symbols. Try the args-aware base first, then the bare one.
        std::string targ_sfx = trait_targ_suffix(current_impl_trait_args_);
        std::string qual_args = std::string(struct_ctx) + "__" +
                           current_impl_trait_name_ + targ_sfx + "__" + std::string(raw_name);
        std::string qual = std::string(struct_ctx) + "__" +
                           current_impl_trait_name_ + "__" + std::string(raw_name);
        if (!targ_sfx.empty() &&
            (func_overloads_.count(qual_args) || generic_overloads_.count(qual_args)))
            mangled = qual_args;
        else if (func_overloads_.count(qual) || generic_overloads_.count(qual))
            mangled = qual;
    }

    // Blanket impl methods are mangled "$blanket$Trait$Bound$T__method".
    // Render diagnostics under a human-readable name so the synthetic prefix
    // never leaks into user-visible errors.
    auto pretty_ctx = [&]() -> std::string {
        if (mangled.rfind("$blanket$", 0) != 0) return std::format("fn {}", mangled);
        std::string rest = mangled.substr(9);
        auto p1 = rest.find('$');
        auto p2 = (p1 != std::string::npos) ? rest.find('$', p1 + 1) : std::string::npos;
        auto p3 = (p2 != std::string::npos) ? rest.find("__", p2 + 1) : std::string::npos;
        if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos)
            return std::format("fn {}", mangled);
        std::string trait_n = rest.substr(0, p1);
        std::string bound   = rest.substr(p1 + 1, p2 - p1 - 1);
        std::string tvar    = rest.substr(p2 + 1, p3 - p2 - 1);
        std::string method  = rest.substr(p3 + 2);
        if (bound.empty())
            return std::format("fn <impl<{}> {}>::{}", tvar, trait_n, method);
        return std::format("fn <impl<{}: {}> {}>::{}", tvar, bound, trait_n, method);
    };
    ctx_       = pretty_ctx();
    node_line_ = get_line(node);
    // Track current fn name so make_drop_stmt can avoid emitting a Drop
    // for the `self` param of a Drop fn (would be infinite self-recursion).
    struct CurFnGuard {
        std::string& slot; std::string prev;
        CurFnGuard(std::string& s, std::string n) : slot(s), prev(s) { slot = std::move(n); }
        ~CurFnGuard() { slot = std::move(prev); }
    };
    CurFnGuard _cfn_guard(current_fn_mangled_, mangled);

    // Some trait-default bodies and impl methods refer to `Self` in their
    // parameter types.  Keep a concrete Self binding alive for the duration
    // of lowering if the surrounding impl context already determined it.
    if (!struct_ctx.empty()) {
        // G153-4 / G141-2: only KEEP an existing Self if it names the SAME type
        // as this method's impl (the impl-block may have set `Self = Foo<T>`
        // with type-args — don't clobber that with a bare `Foo`). A Self left
        // over from a DIFFERENT impl (e.g. a prior `impl Vec`) is STALE and
        // mis-resolved `Self::method()` in this body to the wrong type — so
        // overwrite it. (The `!count` guard alone leaked the stale binding.)
        bool need_set = true;
        if (auto sit = current_type_params_.find("Self");
            sit != current_type_params_.end() && sit->second) {
            auto cur = TypeRef(sit->second);
            std::string cur_name;
            if (cur.kind() == LogosType::Kind::Struct ||
                cur.kind() == LogosType::Kind::ZonedStruct)
                cur_name = std::string(cur.struct_name());
            else if (cur.kind() == LogosType::Kind::Enum)
                cur_name = std::string(cur.enum_name());
            // Strip pkg prefix + generic `$G…` suffix for the bare-name compare.
            if (auto d = cur_name.rfind('.'); d != std::string::npos)
                cur_name = cur_name.substr(d + 1);
            if (auto g = cur_name.find("$G"); g != std::string::npos)
                cur_name = cur_name.substr(0, g);
            if (cur_name == struct_ctx) need_set = false;
        }
        if (need_set) {
            // Prefer datatype Self when a name exists in both tables.
            auto [dpkg, dsi] = find_datatype_by_name(struct_ctx);
            auto [spkg, ssi] = find_struct_by_name(struct_ctx);
            if (dsi)
                current_type_params_["Self"] = make_datatype_type(struct_ctx, dpkg);
            else if (ssi)
                current_type_params_["Self"] = make_struct_type(struct_ctx, spkg);
            else if (auto prim_t = lookup_type_by_name(struct_ctx))
                current_type_params_["Self"] = prim_t;
        }
    }

    lir::LFunction fn;
    fn.name               = mangled;
    fn.from_binary_module = cur_from_binary_;
    fn.from_lazy_module   = cur_from_lazy_;
    fn.doc                = take_pending_doc();
    // Phase #[test] attributes. Consume here so they don't leak into the
    // next fn lowered in the same item-loop iteration.
    fn.is_test            = pending_is_test_;
    fn.should_panic       = pending_should_panic_;
    fn.ignored            = pending_ignore_;
    fn.should_panic_expected_msg = std::move(pending_should_panic_expected_);
    pending_is_test_      = false;
    pending_should_panic_ = false;
    pending_ignore_       = false;
    pending_should_panic_expected_.clear();
    int32_t node_code = code_of(node);
    fn.is_extern = (node_code == la::EXTERN_FN);

    // Check is_vararg for extern fn with variadic params
    if (fn.is_extern && node.has_key(la::IS_VARARG)) {
        AnyVal av = node.get(la::IS_VARARG.code);
        fn.is_vararg = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }

    auto node_tparams = read_type_params(node);
    SemaFuncInfo* fi_ptr = nullptr;
    {
    std::vector<TypeRef> decl_param_types;
    size_t decl_param_arity = 0;
    push_type_params(impl_type_params_);
    push_type_params(node_tparams);
    // g4/K5: desugar `impl Trait` PARAMS into synthetic generic type-params —
    // must mirror collect_fn so the synth params + typevar param types match
    // the registered signature (fi_ptr lookup below compares node_tparams size
    // and decl_param_types).
    impl_param_desugar_active_ = true;
    pending_impl_trait_params_.clear();
    if (node.has_key(la::PARAMS)) {
        auto params_av = node.get(la::PARAMS.code);
        if (params_av.is_pointer()) {
                auto params_node = map_of(params_av);
                if (params_node.has_key(la::ITEMS)) {
                    auto arr = arr_of(params_node.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < arr.size(); ++i) {
                        auto p = map_of(arr.get(i));
                        if (code_of(p) != la::PARAM) continue;
                        if (p.has_key(la::TYPE)) {
                            TypeRef pt = resolve_type(map_of(p.get(la::TYPE.code)));
                            std::string pname = p.has_key(la::NAME)
                                ? std::string(str_of(p.get(la::NAME.code)))
                                : "_";
                            if (pt && TypeRef(pt).kind() == LogosType::Kind::Void) {
                                error(std::format(
                                    "parameter '{}' has unit type '()'; "
                                    "unit-typed parameters carry no information",
                                    pname));
                                pt = error_t();
                            } else if (pt && TypeRef(pt).kind() == LogosType::Kind::ImplTrait) {
                                error(std::format(
                                    "parameter '{}': 'impl Trait' is not yet "
                                    "supported at parameter position; use "
                                    "an explicit generic 'fn f<T: {}>(x: T)' "
                                    "or '&dyn {}'",
                                    pname, TypeRef(pt).struct_name(),
                                    TypeRef(pt).struct_name()));
                                pt = error_t();
                            }
                            decl_param_types.push_back(pt);
                        }
                        else {
                            auto self_t = current_type_params_.count("Self")
                                ? current_type_params_.at("Self") : error_t();
                            bool is_mut = p.has_key(la::IS_MUT) &&
                                          !p.get(la::IS_MUT.code).is_null() &&
                                          p.get(la::IS_MUT.code).as_value<uint8_t>() != 0;
                            decl_param_types.push_back(make_ref(is_mut, self_t));
                    }
                }
            }
        }
    }
    impl_param_desugar_active_ = false;
    for (auto& tp : pending_impl_trait_params_) node_tparams.push_back(tp);
    pending_impl_trait_params_.clear();
    decl_param_arity = decl_param_types.size();
    // Resolve any AssocType / alias placeholders so the param signature
    // matches what sema_collect's add_func saved (collect already runs
    // assoc-type resolution before mangling). Otherwise types_equal in
    // the candidate-matching loop fails for fns whose params are
    // associated types (e.g. `Box<i32>::Inner<i32>` -> i32).
    for (auto& pt : decl_param_types)
        pt = subst_type_sema(pt, {});
    pop_type_params(node_tparams);
    pop_type_params(impl_type_params_);

        // Match by (type_params arity, param signature). When this declaration
        // has type params, a non-generic same-name overload must NOT win — both
        // can have empty value-param lists (e.g. `fn f() -> u64` vs
        // `fn f<T...>() -> u64`) and only the type-params arity disambiguates.
        for (auto* cand : find_func_candidates(mangled)) {
            if (!cand || cand->type_params.size() != node_tparams.size()) continue;
            if (cand->param_types.size() != decl_param_types.size()) continue;
            bool same = true;
            for (size_t i = 0; i < decl_param_types.size(); ++i) {
                if (!cand->param_types[i] || !decl_param_types[i] ||
                    !types_equal(cand->param_types[i], decl_param_types[i])) {
                    same = false; break;
                }
            }
            if (same) { fi_ptr = const_cast<SemaFuncInfo*>(cand); break; }
        }
        if (!fi_ptr) {
            if (auto fit = find_func_by_base_and_signature(mangled, decl_param_types, fn.is_vararg))
                fi_ptr = const_cast<SemaFuncInfo*>(fit);
        }
        // Relaxed method match for overloaded members: if only `self` disagrees
        // (e.g. Struct-vs-Datatype Self representation), still accept candidate
        // when all explicit parameters match exactly.
        if (!fi_ptr && !struct_ctx.empty() && decl_param_types.size() >= 1) {
            for (auto* cand : find_func_candidates(mangled)) {
                if (!cand || cand->type_params.size() != node_tparams.size()) continue;
                if (cand->param_types.size() != decl_param_types.size()) continue;
                bool same_tail = true;
                for (size_t i = 1; i < decl_param_types.size(); ++i) {
                    if (!cand->param_types[i] || !decl_param_types[i] ||
                        !types_equal(cand->param_types[i], decl_param_types[i])) {
                        same_tail = false;
                        break;
                    }
                }
                if (same_tail) { fi_ptr = const_cast<SemaFuncInfo*>(cand); break; }
            }
        }
        // Method-decl fallback: some parser paths provide PARAMS without explicit
        // `self` in the reconstructed decl signature. For overloaded methods this
        // makes exact arity matching fail and leaves fi_ptr unresolved.
        if (!fi_ptr && !struct_ctx.empty()) {
            TypeRef self_t = nullptr;
            {
                auto [dpkg_sc, dsi_sc] = find_datatype_by_name(struct_ctx);
                auto [spkg_sc, ssi_sc] = find_struct_by_name(struct_ctx);
                if (dsi_sc)      self_t = make_datatype_type(struct_ctx, dpkg_sc);
                else if (ssi_sc) self_t = make_struct_type(struct_ctx, spkg_sc);
            }
            if (self_t) {
                for (auto* cand : find_func_candidates(mangled)) {
                    if (!cand || cand->type_params.size() != node_tparams.size()) continue;
                    if (cand->param_types.size() != decl_param_types.size() + 1) continue;
                    auto self_param = cand->param_types[0];
                    TypeRef spv{self_param};
                    if (!self_param ||
                        (spv.kind() != LogosType::Kind::Ref &&
                         spv.kind() != LogosType::Kind::MutRef &&
                         spv.kind() != LogosType::Kind::Ptr) ||
                        !spv.pointee() ||
                        !types_equal(spv.pointee(), self_t))
                        continue;
                    bool same_tail = true;
                    for (size_t i = 0; i < decl_param_types.size(); ++i) {
                        auto dt = decl_param_types[i];
                        auto pt = cand->param_types[i + 1];
                        if (!dt || !pt || !types_equal(dt, pt)) {
                            same_tail = false;
                            break;
                        }
                    }
                    if (same_tail) { fi_ptr = const_cast<SemaFuncInfo*>(cand); break; }
                }
            }
        }
        if (!fi_ptr) {
            if (auto fit = find_func_by_symbol(mangled)) {
                fi_ptr = const_cast<SemaFuncInfo*>(fit);
            }
            if (!fi_ptr)
                fi_ptr = const_cast<SemaFuncInfo*>(find_generic_func(mangled, decl_param_arity));
            if (!fi_ptr)
                fi_ptr = const_cast<SemaFuncInfo*>(find_generic_func(mangled));
            // Primitive-target impl (`impl Eq for i32 { fn eq … }`) doesn't
            // resolve via self_t (i32 isn't a struct/datatype). Fall back
            // to base-name candidate matching with arity. Only for impl
            // methods (struct_ctx non-empty) — free fns must use the
            // strict matching above.
            if (!fi_ptr && !struct_ctx.empty()) {
                // Match on arity. For primitive-target impls, decl_param_types
                // already includes `self` (collect_fn for primitives runs
                // without struct context flag). For struct/datatype impls,
                // decl_param_types excludes self (added back via self_t).
                for (auto* cand : find_func_candidates(mangled)) {
                    if (!cand) continue;
                    if (cand->type_params.size() != node_tparams.size()) continue;
                    if (cand->param_types.size() != decl_param_types.size() &&
                        cand->param_types.size() != decl_param_types.size() + 1)
                        continue;
                    fi_ptr = const_cast<SemaFuncInfo*>(cand); break;
                }
            }
        }
    }
    if (!fi_ptr) return fn;   // shouldn't happen after collect

    fn.name           = fi_ptr->symbol_name.empty() ? mangled : fi_ptr->symbol_name;
    fn.method_base    = std::string(raw_name);
    fn.package        = fi_ptr->package;
    fn.source_file    = fi_ptr->source_file;
    fn.type_params    = fi_ptr->type_params;
    fn.lifetime_params = read_lifetime_params(node);
    // Lifetime-param uniqueness on fn (closes B-gn-02)
    check_unique_names(fn.lifetime_params,
                       [](auto& lt) -> std::string_view { return lt; },
                       "lifetime parameter", "fn " + mangled);
    // Robust associated type resolution: call subst_type_sema even if subst is empty
    // to simplify concrete AssocType nodes (e.g. i32::Item -> bool).
    fn.ret_type    = subst_type_sema(fi_ptr->ret_type, {});
    ret_type_      = fn.ret_type;
    // Bug 5 fix: DataNode enforcement covers both bare Datatype and Array-of-Datatype
    // return/param types.  Extract the innermost non-Array element for the check.
    auto datanode_name = [&](TypeRef t) -> std::string {
        if (!t) return {};
        while (TypeRef(t).kind() == LogosType::Kind::Array) t = TypeRef(t).elem();
        TypeRef tv{t};
        if (tv.kind() == LogosType::Kind::ZonedStruct && tv.type_args().empty()) {
            auto* dsi = get_datatype_si(t);
            if (dsi && !dsi->is_data_plain)
                return std::string(tv.struct_name());
        }
        return {};
    };
    // DataNode enforcement on return type.
    {
        auto dn = datanode_name(fn.ret_type);
        if (!dn.empty()) {
            error(std::format(
                "return type '{}' is a DataNode eidos — cannot be returned by value. "
                "Return DataRef<{}> instead.", dn, dn));
        }
    }
    // Reset impl-trait inference state for this function.
    if (fn.ret_type && TypeRef(fn.ret_type).kind() == LogosType::Kind::ImplTrait)
        impl_ret_type_inferred_ = nullptr;

    // Put type params in scope for the duration of the function body
    push_type_params(fn.type_params);
    // Type-param uniqueness on fn (B-gn-01 family)
    check_unique_names(fn.type_params,
                       [](auto& tp) -> std::string_view { return tp.name; },
                       "type parameter", "fn " + mangled);

    scope_.clear();
    push_scope();
    // Reset move-tracking at each function boundary. Without this, a synthetic
    // variable name reused across functions — notably format!'s `__buf` — keeps
    // its moved state from a previous function, so the second function using
    // `format!` reports a spurious "use of moved variable '__buf'". User-named
    // locals never leaked (distinct scopes/names), which masked this.
    moved_vars_.clear();

    // P4-pm-19: tuple-destructure parameters. Track synth-name +
    // user-name list for each; after the body is lowered, prepend
    // `let user_k = synth.k;` for each element (parallel to closure
    // C5-cl-07).
    struct TupleFnParam {
        std::vector<std::string> users;
        std::string              synth;
        TypeRef                  ty;
    };
    std::vector<TupleFnParam> fn_tuple_params;

    // `fn f(mut x: T)` — {user_name, synth_param_name, type}. Prologue emits
    // `let mut user = synth;` so the body sees a mutable, alloca-backed local.
    struct MutFnParam {
        std::string user;
        std::string synth;
        TypeRef     ty;
    };
    std::vector<MutFnParam> fn_mut_params;

    // Parameters
    if (node.has_key(la::PARAMS)) {
        auto params_av = node.get(la::PARAMS.code);
        if (params_av.is_pointer()) {
            auto params_node = map_of(params_av);
            if (params_node.has_key(la::ITEMS)) {
                auto arr = arr_of(params_node.get(la::ITEMS.code));
                for (uint64_t i = 0; i < arr.size(); ++i) {
                    auto p = map_of(arr.get(i));
                    if (code_of(p) != la::PARAM) continue;
                    auto ptype = (i < fi_ptr->param_types.size())
                        ? fi_ptr->param_types[i] : error_t();
                    TypeRef pt = subst_type_sema(ptype, {});
                    // The never type `!` has no values, so a `!`-typed parameter
                    // is uninhabited — the fn can never be called. Reject it
                    // with a diagnostic (it has no MLIR representation, so
                    // letting it through SIGSEGVs codegen). `!` stays valid in
                    // RETURN position (a diverging fn).
                    if (pt && TypeRef(pt).kind() == LogosType::Kind::Never) {
                        error(std::format(
                            "parameter '{}': the never type `!` is uninhabited and "
                            "cannot be a parameter type",
                            p.has_key(la::NAME) ? std::string(str_of(p.get(la::NAME.code))) : std::string("_")));
                        pt = error_t();
                    }

                    // P4-pm-19: tuple-destructure form
                    // `(a, b): (T1, T2)` — synth a name + register the
                    // user-bindings for body-prologue rewrite.
                    if (p.has_key(la::NAMES)) {
                        auto nav = p.get(la::NAMES.code);
                        if (!nav.is_null() && nav.is_pointer()) {
                            auto nmap = map_of(nav);
                            if (nmap.has_key(la::ITEMS)) {
                                auto narr = arr_of(nmap.get(la::ITEMS.code));
                                std::vector<std::string> users;
                                for (uint64_t k = 0; k < narr.size(); ++k) {
                                    auto sub = map_of(narr.get(k));
                                    if (code_of(sub) == la::PAT_WILD &&
                                        sub.has_key(la::NAME))
                                        users.emplace_back(
                                            str_of(sub.get(la::NAME.code)));
                                    else
                                        users.emplace_back("_");
                                }
                                std::string synth = std::format(
                                    "__tup_param_{}__{}", mangled, i);
                                define(synth, pt);
                                if (TypeRef(pt).kind() == LogosType::Kind::Tuple) {
                                    auto elems = TypeRef(pt).tuple_elems();
                                    for (size_t k = 0; k < users.size() && k < elems.size(); ++k)
                                        if (users[k] != "_")
                                            define(users[k], elems[k]);
                                }
                                fn_tuple_params.push_back({std::move(users), synth, pt});
                                fn.params.push_back({synth, pt, false});
                                continue;
                            }
                        }
                    }

                    auto pname = str_of(p.get(la::NAME.code));
                    // B-fn-10: reject `self` as a parameter name outside of
                    // impl-block context.  Inside an impl, `self` (typically
                    // first) is the magic receiver; outside, it's misleading.
                    if (pname == "self" && struct_ctx.empty()) {
                        error("parameter 'self' is reserved for impl-block "
                              "method receivers; use a different name in a "
                              "standalone function");
                    }
                    bool p_variadic = false;
                    if (p.has_key(la::IS_VARIADIC)) {
                        AnyVal av = p.get(la::IS_VARIADIC.code);
                        p_variadic = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
                    }
                    // DataNode enforcement: DataNode datatypes (has relative-pointer fields)
                    // cannot be passed by bare value — the relative pointers require a base
                    // pointer that is not available without zone context.  Use DataRef<T>.
                    // Bug 5 fix: also covers Array-of-DataNode parameter types.
                    {
                        auto dn = datanode_name(pt);
                        if (!dn.empty()) {
                            error(std::format(
                                "parameter '{}': DataNode eidos '{}' cannot be passed by "
                                "value — it contains relative pointers that require a zone "
                                "base pointer. Use DataRef<{}> instead.",
                                pname, dn, dn));
                        }
                    }
                    // `fn f(mut x: T)` — the parameter is a mutable local
                    // binding (Rust-style; caller-invisible: it just lets the
                    // body reassign / take `&mut` of the param). IS_MUT on a
                    // TYPED param means binding-mutability (distinct from the
                    // self-param IS_MUT, which is reference-mutability handled
                    // at signature collection — those have no TYPE).
                    // Desugar via the same synth-param + prologue-let mechanism
                    // as tuple-destructure params: the parameter takes a synth
                    // name (immutable SSA), and a prologue `let mut x = synth;`
                    // materializes a mutable, alloca-backed local the body sees.
                    bool p_is_mut = false;
                    if (p.has_key(la::TYPE) && p.has_key(la::IS_MUT)) {
                        AnyVal mv = p.get(la::IS_MUT.code);
                        p_is_mut = !mv.is_null() && mv.is_value() && mv.as_value<uint8_t>() != 0;
                    }
                    if (p_is_mut && !p_variadic) {
                        std::string synth = std::format("__mutparam_{}__{}", mangled, i);
                        // NOTE: deliberately do NOT define(synth) in sema scope.
                        // The prologue `let mut user = synth;` MOVES the param
                        // value into the user local; if synth were a tracked
                        // scope var it would also get drop glue → double-free on
                        // move types. mlir-gen still binds scope_[synth] from
                        // fn.params, so the prologue's var_ref(synth) resolves.
                        define(std::string(pname), pt, /*is_mut=*/true);  // body sees mutable local
                        fn_mut_params.push_back({std::string(pname), synth, pt});
                        fn.params.push_back({synth, pt, false});
                        continue;
                    }
                    define(pname, pt);
                    fn.params.push_back({std::string(pname), pt, p_variadic});
                }
            }
        }
    }
    // Param-name uniqueness (closes B-fn-02)
    check_unique_names(fn.params,
                       [](auto& p) -> std::string_view { return p.name; },
                       "parameter", "fn " + mangled);

    // B65: outlives bounds — capture explicit + implied + where-clause +
    // type-outlives. Placed AFTER fn.params + fn.ret_type so the implied-
    // bounds walker sees the resolved signature.
    compute_fn_lifetime_outlives(node, fn);
    current_outlives_ = fn.lifetime_outlives;  // B64/B65: visible to coercion sites

    // unsafe fn body is implicitly an unsafe context
    bool was_unsafe = inside_unsafe_;
    if (fi_ptr->is_unsafe) inside_unsafe_ = true;

    // Metaprog pre-sema mode: in the entry AST, skip body lowering for fns
    // that are NOT registered metaprog handlers. The entry file may reference
    // symbols (impls, types) that don't exist yet — they will be synthesized
    // by handler hooks. Handlers themselves must be fully lowered so the JIT
    // can compile them.
    //
    // Impl methods (struct_ctx non-empty) are NEVER stubbed — their bodies
    // are reachable from stdlib generic chains (e.g. Ord blanket → Container
    // ::cmp_view_key → user's K::cmp), and stubbing them out would leave
    // dangling call-site references in the metaprog mlir module. User-side
    // impl methods don't reference yet-to-be-synthesized symbols anyway;
    // they're concrete code that already exists in the source.
    // Stub the entry ast PLUS derive-blob-emitted synth docs (filename
    // = "<metaprog-blob-subst>"). Their fn bodies may reference user-
    // side fns that are themselves stubs in this metaprog discovery
    // pass; lowering them would dangle in the meta_prog mlir module.
    //
    // Don't stub metacall thunks emitted via logos_emit_source (filename
    // "<metaprog>") — those need to JIT-compile so the discovery loop
    // can invoke them. The final non-metaprog sema pass lowers
    // everything for real.
    bool is_synth_blob = file_ == "<metaprog-blob-subst>"
                      || file_ == "<test_main_synth>";
    // Impl methods on a yet-to-be-derived target struct (e.g. cow.logos's
    // `impl BTreeNode for BranchNode { ... }` where BranchNode is emitted
    // by a #[derive_branch_node] hook on a sibling struct) cascade if
    // lowered: their bodies reference fields/methods of the unresolved
    // target. Skip them in metaprog discovery — the post-dispatch sema
    // pass lowers them for real once the derive has fired. Safe because
    // no stdlib generic chain can reach into a struct that doesn't exist.
    bool impl_target_unresolved = metaprog_mode_
                               && !struct_ctx.empty()
                               && struct_ctx.find("$blanket$") == std::string::npos
                               && !find_struct_by_name(struct_ctx).second
                               && !find_datatype_by_name(struct_ctx).second;
    bool skip_body = metaprog_mode_
                  && (cur_ast_idx_ == metaprog_entry_ast_idx_ || is_synth_blob)
                  && (struct_ctx.empty() || impl_target_unresolved)
                  && !fn_is_metaprog_handler(fn.name)
                  && !fn_is_metaprog_keep(fn.name);
    if (skip_body) fn.is_metaprog_stub = true;

    // Skeleton-only lowering for from_binary fns whose body is already
    // compiled into a linked archive's .o. Lowering the body here is pure
    // overhead: type-checking a call needs only the signature, mlir_gen
    // forward-declares from_binary fns (is_binary_skip), and mono takes the
    // signature-only clone path for binary symbols (no scan_fn). So the body
    // is never used — we skip producing it and link the symbol from the .o.
    //
    // Gate = "the symbol is in binary_symbols_" (the nm --defined-only set of
    // the linked archives) — the same membership test mlir_gen's is_binary_skip
    // uses, so sema-skip and codegen-forward-declare stay in lockstep:
    //   - non-generic free fn   → name IS in the dep .o → skip, link it
    //   - generic template      → only INSTANTIATIONS are in the .o, not the
    //                              template name → not skipped → lowered for
    //                              consumer-side instantiation
    //   - non-generic method of a generic struct → not a concrete .o symbol
    //                              → not skipped → lowered per instantiation
    // Self-gating for library builds: own (not-yet-compiled) fns are absent
    // from binary_symbols_, so their bodies are lowered locally and mono's
    // scan_fn still discovers their generic calls.
    //
    // Carve-outs: metaprog handlers / metaprog-keep fns can be invoked at
    // compile time and need bodies. Specializations go through lower_spec_fn.
    bool skel_skip_body = cur_from_binary_
                       && !fn.is_extern
                       && !fn_is_metaprog_handler(fn.name)
                       && !fn_is_metaprog_keep(fn.name)
                       && binary_symbols_
                       && binary_symbols_->count(fn.name) > 0;
    if (skel_skip_body) {
        skip_body = true;
        ++skel_skip_count_;
    }

    // Body (extern fns have no body)
    if (!fn.is_extern && !skip_body && node.has_key(la::BODY)) {
        auto body_node = map_of(node.get(la::BODY.code));
        // Detect if the last stmt in the function body is a match.
        // If so, set the flag so lower_match treats EXPR arms as return values.
        if (fn.ret_type && TypeRef(fn.ret_type).kind() != LogosType::Kind::Void) {
            if (body_node.has_key(la::ITEMS)) {
                auto stmts = arr_of(body_node.get(la::ITEMS.code));
                // Find last non-null stmt
                for (int64_t si = (int64_t)stmts.size() - 1; si >= 0; --si) {
                    auto s = map_of(stmts.get(si));
                    if (!s.is_null()) {
                        match_in_tail_position_ = (code_of(s) == la::MATCH);
                        break;
                    }
                }
            }
        }
        bool saved_tail_as_return = tail_as_return_;
        tail_as_return_ = true;
        fn.body = lower_block(body_node);
        tail_as_return_ = saved_tail_as_return;
        match_in_tail_position_ = false;
        // P4-pm-19: prepend `let user_k = synth.k;` for each tuple-
        // destructure parameter so body sees the user-visible names.
        if (!fn_tuple_params.empty()) {
            std::vector<lir::LStmt> prologue;
            for (auto& tp : fn_tuple_params) {
                if (TypeRef(tp.ty).kind() != LogosType::Kind::Tuple) continue;
                auto elems = TypeRef(tp.ty).tuple_elems();
                for (size_t k = 0; k < tp.users.size() && k < elems.size(); ++k) {
                    if (tp.users[k] == "_") continue;
                    lir::SLet sl;
                    sl.name   = tp.users[k];
                    sl.type   = elems[k];
                    sl.is_mut = false;
                    sl.value  = builder().tuple_index(
                        builder().var_ref(tp.synth, tp.ty),
                        (uint32_t)k, elems[k]);
                    prologue.push_back(make_stmt_emit(node_line_, std::move(sl)));
                }
            }
            prologue.insert(prologue.end(),
                            std::make_move_iterator(fn.body.stmts.begin()),
                            std::make_move_iterator(fn.body.stmts.end()));
            fn.body.stmts = std::move(prologue);
        }
        // `mut x: T` params — prepend `let mut x = synth;` so the body's
        // mutable local is materialized from the (immutable) synth parameter.
        if (!fn_mut_params.empty()) {
            std::vector<lir::LStmt> prologue;
            for (auto& mp : fn_mut_params) {
                lir::SLet sl;
                sl.name   = mp.user;
                sl.type   = mp.ty;
                sl.is_mut = true;
                sl.value  = builder().var_ref(mp.synth, mp.ty);
                prologue.push_back(make_stmt_emit(node_line_, std::move(sl)));
            }
            prologue.insert(prologue.end(),
                            std::make_move_iterator(fn.body.stmts.begin()),
                            std::make_move_iterator(fn.body.stmts.end()));
            fn.body.stmts = std::move(prologue);
        }
        // Resolve impl Trait return type to the concrete type inferred from returns.
        if (fn.ret_type && TypeRef(fn.ret_type).kind() == LogosType::Kind::ImplTrait) {
            if (impl_ret_type_inferred_) {
                fn.ret_type       = impl_ret_type_inferred_;
                fi_ptr->ret_type  = impl_ret_type_inferred_;
                ret_type_         = impl_ret_type_inferred_;
            } else {
                error("impl Trait return: could not infer concrete return type");
            }
        }
        // Return reachability check (on AST node — before scope is gone).
        // Re-set tail_as_return_ for the duration so block_always_returns
        // (and recursive stmt_always_returns) treat trailing TAIL_EXPRs in
        // fn-body's transitive control flow as implicit returns.
        bool saved_check_flag = tail_as_return_;
        tail_as_return_ = true;
        if (fn.ret_type && TypeRef(fn.ret_type).kind() != LogosType::Kind::Void &&
            TypeRef(fn.ret_type).kind() != LogosType::Kind::Error &&
            !block_always_returns(body_node)) {
            error("not all paths return a value");
        }
        tail_as_return_ = saved_check_flag;
    }

    inside_unsafe_ = was_unsafe;
    pop_scope();
    pop_type_params(fn.type_params);
    return fn;
}

lir::LStructDef SemaChecker::lower_struct_def(TinyMapView node) {
    auto sname = std::string(str_of(node.get(la::NAME.code)));
    lir::LStructDef sd;
    sd.name               = sname;
    sd.pkg                = cur_package_;
    sd.from_binary_module = cur_from_binary_;
    // Look up in structs_ or datatypes_ — never default-insert via operator[].
    // Use package-aware find helpers so cross-package lowering works.
    auto [spkg_sd, ssi_sd] = find_struct_by_name(sname);
    auto [dpkg_sd, dsi_sd] = find_datatype_by_name(sname);
    SemaStructInfo* sinfo = nullptr;
    if      (ssi_sd) sinfo = ssi_sd;
    else if (dsi_sd) sinfo = dsi_sd;
    else {
        // Fall back to qualified key (cur_package_ set by lower_module_items)
        auto qkey = sema_key(cur_package_, sname);
        auto sit2  = structs_.find(qkey);
        auto doit2 = datatypes_.find(qkey);
        if      (sit2  != structs_.end())   sinfo = &sit2->second;
        else if (doit2 != datatypes_.end()) sinfo = &doit2->second;
        else {
            error(std::format("internal: '{}' not found in collect phase", sname));
            return sd;
        }
    }
    sd.type_params = sinfo->type_params;
    sd.lifetime_params = sinfo->lifetime_params;
    // Phase 1B-14: propagate custom-DST flag from sema info to LIR.
    sd.is_dst = sinfo->is_dst;
    // B65: outlives bounds from `struct Foo<'a, 'b: 'a>` + validate names.
    sd.lifetime_outlives = read_lifetime_outlives(node);
    // B68.3: also pick up where-clause outlives + type-outlives bounds.
    {
        auto where_outlives = read_lifetime_outlives_from(node, la::WHERE.code);
        for (auto& p : where_outlives) sd.lifetime_outlives.push_back(std::move(p));
        if (node.has_key(la::WHERE)) {
            AnyVal wav = node.get(la::WHERE.code);
            if (!wav.is_null()) {
                auto wmap = map_of(wav);
                if (wmap.has_key(la::ITEMS)) {
                    auto witems = arr_of(wmap.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < witems.size(); ++i) {
                        auto witem = map_of(witems.get(i));
                        if (code_of(witem) != la::TYPE_PARAM) continue;
                        if (!witem.has_key(la::ITEMS)) continue;
                        std::string tname(str_of(witem.get(la::NAME.code)));
                        auto inner = arr_of(witem.get(la::ITEMS.code));
                        TypeParam* target = nullptr;
                        for (auto& tp : sd.type_params)
                            if (tp.name == tname) { target = &tp; break; }
                        if (!target) continue;
                        for (uint64_t j = 0; j < inner.size(); ++j) {
                            auto inode = map_of(inner.get(j));
                            if (code_of(inode) != la::LIFETIME_PARAM) continue;
                            target->lifetime_outlives.push_back(
                                std::string(str_of(inode.get(la::NAME.code))));
                        }
                    }
                }
            }
        }
    }
    {
        std::unordered_set<std::string> declared(sd.lifetime_params.begin(),
                                                 sd.lifetime_params.end());
        auto known = [&](std::string_view lt) {
            if (lt.empty()) return true;
            if (lt == "'static" || lt == "static") return true;
            return declared.count(std::string(lt)) > 0;
        };
        for (auto& [lng, sht] : sd.lifetime_outlives) {
            if (!known(lng))
                error(std::format("struct '{}': use of undeclared lifetime name '{}' in outlives clause",
                                  sname, lng));
            if (!known(sht))
                error(std::format("struct '{}': use of undeclared lifetime name '{}' in outlives clause",
                                  sname, sht));
        }
        for (auto& tp : sd.type_params) {
            for (auto& lt : tp.lifetime_outlives) {
                if (!known(lt))
                    error(std::format("struct '{}': use of undeclared lifetime name '{}' in `{}: {}` bound",
                                      sname, lt, tp.name, lt));
            }
        }
    }
    push_type_params(sd.type_params);
    // Type-param uniqueness (closes B-gn-01)
    check_unique_names(sd.type_params,
                       [](auto& tp) -> std::string_view { return tp.name; },
                       "type parameter", "struct " + sname);
    // Lifetime-param uniqueness (closes B-gn-02)
    check_unique_names(sd.lifetime_params,
                       [](auto& lt) -> std::string_view { return lt; },
                       "lifetime parameter", "struct " + sname);
    for (auto& f : sinfo->fields)
        sd.fields.push_back({std::string(f.name), f.type, f.is_variadic, f.doc});
    // Field-name uniqueness (closes B-it-03)
    check_unique_names(sd.fields,
                       [](auto& f) -> std::string_view { return f.name; },
                       "field", "struct " + sname);
    if (node.has_key(la::ITEMS)) {
        auto methods = arr_of(node.get(la::ITEMS.code));
        // Phase A.2: doc-comments in the method stream prime pending_doc_
        // for the next lower_fn invocation.
        pending_doc_.clear();
        for (uint64_t m = 0; m < methods.size(); ++m) {
            auto method = map_of(methods.get(m));
            if (try_append_doc(pending_doc_, method)) continue;
            int32_t mc = code_of(method);
            if (mc == la::FN || mc == la::STATIC_FN)
                sd.methods.push_back(std::make_unique<lir::LFunction>(lower_fn(method, sname)));
        }
        pending_doc_.clear();
    }
    pop_type_params(sd.type_params);
    return sd;
}

lir::LEnumDef SemaChecker::lower_enum_def(TinyMapView node) {
    auto ename = std::string(str_of(node.get(la::NAME.code)));
    lir::LEnumDef ed;
    ed.name = ename;
    ed.pkg  = cur_package_;
    auto [epkg_led, esi_led] = find_enum_by_name(ename);
    auto eit_led = esi_led ? enums_.find(sema_key(epkg_led, ename)) : enums_.end();
    if (eit_led == enums_.end()) eit_led = enums_.find(ename);
    if (eit_led == enums_.end()) {
        error(std::format("internal: enum '{}' not found in lower_enum_def", ename));
        return ed;
    }
    auto& einfo = eit_led->second;
    ed.type_params = einfo.type_params;
    ed.backing_type = einfo.backing_type;
    // B65: capture outlives bounds. Enum lifetime_params lives on einfo;
    // outlives bounds re-read from the node.
    ed.lifetime_params = einfo.lifetime_params;
    ed.lifetime_outlives = read_lifetime_outlives(node);
    // B68.3: also pick up where-clause outlives + type-outlives.
    {
        auto where_outlives = read_lifetime_outlives_from(node, la::WHERE.code);
        for (auto& p : where_outlives) ed.lifetime_outlives.push_back(std::move(p));
        if (node.has_key(la::WHERE)) {
            AnyVal wav = node.get(la::WHERE.code);
            if (!wav.is_null()) {
                auto wmap = map_of(wav);
                if (wmap.has_key(la::ITEMS)) {
                    auto witems = arr_of(wmap.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < witems.size(); ++i) {
                        auto witem = map_of(witems.get(i));
                        if (code_of(witem) != la::TYPE_PARAM) continue;
                        if (!witem.has_key(la::ITEMS)) continue;
                        std::string tname(str_of(witem.get(la::NAME.code)));
                        auto inner = arr_of(witem.get(la::ITEMS.code));
                        TypeParam* target = nullptr;
                        for (auto& tp : ed.type_params)
                            if (tp.name == tname) { target = &tp; break; }
                        if (!target) continue;
                        for (uint64_t j = 0; j < inner.size(); ++j) {
                            auto inode = map_of(inner.get(j));
                            if (code_of(inode) != la::LIFETIME_PARAM) continue;
                            target->lifetime_outlives.push_back(
                                std::string(str_of(inode.get(la::NAME.code))));
                        }
                    }
                }
            }
        }
    }
    {
        std::unordered_set<std::string> declared(ed.lifetime_params.begin(),
                                                 ed.lifetime_params.end());
        auto known = [&](std::string_view lt) {
            if (lt.empty()) return true;
            if (lt == "'static" || lt == "static") return true;
            return declared.count(std::string(lt)) > 0;
        };
        for (auto& [lng, sht] : ed.lifetime_outlives) {
            if (!known(lng))
                error(std::format("enum '{}': use of undeclared lifetime name '{}' in outlives clause",
                                  ename, lng));
            if (!known(sht))
                error(std::format("enum '{}': use of undeclared lifetime name '{}' in outlives clause",
                                  ename, sht));
        }
        for (auto& tp : ed.type_params) {
            for (auto& lt : tp.lifetime_outlives) {
                if (!known(lt))
                    error(std::format("enum '{}': use of undeclared lifetime name '{}' in `{}: {}` bound",
                                      ename, lt, tp.name, lt));
            }
        }
    }
    // Type-param uniqueness on enum (B-gn-01 family)
    check_unique_names(ed.type_params,
                       [](auto& tp) -> std::string_view { return tp.name; },
                       "type parameter", "enum " + ename);
    for (auto& v : einfo.variants)
        ed.variants.push_back({std::string(v.name), v.value, v.payload_types, v.is_variadic, v.doc});
    // Variant-name uniqueness (closes B-it-04)
    check_unique_names(ed.variants,
                       [](auto& v) -> std::string_view { return v.name; },
                       "variant", "enum " + ename);
    return ed;
}


lir::LConst SemaChecker::lower_const_def(TinyMapView node) {
    auto name = std::string(str_of(node.get(la::NAME.code)));
    lir::LConst lc;
    lc.name = name;
    auto cit = module_consts_.find(name);
    lc.type = (cit != module_consts_.end()) ? cit->second : error_t();
    // Const arrays/tuples: mlir-gen's EVarRef-for-const path re-
    // evaluates the const's value at each use-site, so a literal like
    // `["a", "b"]` materialises a fresh on-stack array each access.
    // Functionally correct as long as the const is only read from
    // ordinary (non-metaprog-AST) code; metaprog handlers walk the
    // AST, not the runtime value, so they're unaffected. Real rodata-
    // global lowering would optimise this but isn't required.
    // For generic consts, push the const's type-params so any `<type:T>`
    // inside the value AST resolves to the param TypeVar (not an unbound
    // name).  The actual concrete instantiations happen per use-site via
    // resolve_hstatic_value; this lowering only needs the names in scope so
    // diagnostics see them as type-params instead of bogus unknowns.
    auto git = generic_consts_.find(name);
    std::vector<std::string> pushed_params;
    if (git != generic_consts_.end()) {
        for (auto& tp : git->second.type_params) {
            if (current_type_params_.count(tp.name)) continue;
            current_type_params_[tp.name] = make_typevar(tp.name);
            pushed_params.push_back(tp.name);
        }
    }
    if (node.has_key(la::VALUE)) {
        lc.value = lower_expr(map_of(node.get(la::VALUE.code)));
        // B-ca-02: typecheck initializer against declared const type at sema
        // so the diagnostic surfaces here rather than at MLIR-verifier time.
        if (lc.type && lc.value && lc.value->type &&
            TypeRef(lc.type).kind() != LogosType::Kind::Error &&
            TypeRef(lc.value->type).kind() != LogosType::Kind::Error &&
            !types_compatible(lc.value->type, lc.type)) {
            // HermesStatic special-case: literal evaluates to HStaticLit,
            // which is treated as compatible with HermesStatic at higher level.
            bool hs_ok = TypeRef(lc.type).kind() == LogosType::Kind::Struct &&
                         is_hermes_static(lc.type) &&
                         TypeRef(lc.value->type).kind() == LogosType::Kind::HStaticLit;
            if (!hs_ok) {
                auto [es, gs] = type_str_pair(lc.type, lc.value->type);
                error(std::format(
                    "const '{}': initializer type mismatch — expected {}, got {}",
                    name, es, gs));
            }
        }
    } else {
        lc.value = error_expr();
    }
    for (auto& n : pushed_params) current_type_params_.erase(n);
    return lc;
}

lir::LTypeAlias SemaChecker::lower_type_alias_def(TinyMapView node) {
    auto name = std::string(str_of(node.get(la::NAME.code)));
    lir::LTypeAlias ta;
    ta.name = name;
    auto ait = type_aliases_.find(name);
    // Generic aliases have no concrete LIR type (they're inlined at use sites).
    ta.type = (ait != type_aliases_.end() && ait->second.type_params.empty())
              ? ait->second.type : error_t();
    return ta;
}

lir::LTraitDef SemaChecker::lower_trait_def(TinyMapView node) {
    auto tname = std::string(str_of(node.get(la::NAME.code)));
    lir::LTraitDef td;
    td.name = tname;
    auto tit = traits_.find(tname);
    if (tit != traits_.end()) {
        for (auto& at : tit->second.assoc_types)
            td.assoc_types.push_back({at.name, at.bounds, at.doc});
        for (auto& m : tit->second.methods) {
            lir::LTraitMethodSig sig;
            sig.name     = m.name;
            sig.ret_type = m.ret_type;
            sig.doc      = m.doc;
            // We don't lower params here since they may contain Self
            td.methods.push_back(std::move(sig));
        }
    }
    td.pkg = std::string(cur_package_);
    if (tit != traits_.end())
        td.is_auto = tit->second.is_auto;
    if (node.has_key(la::TYPE_PARAMS)) {
        auto tps = read_type_params(node);
        for (auto& tp : tps)
            td.type_params.push_back(tp.name);
    }
    return td;
}

void SemaChecker::lower_impl_block(TinyMapView node, lir::LProgram& prog) {
    std::string impl_doc = take_pending_doc();
    std::string trait_name;
    if (node.has_key(la::NAME))
        trait_name = std::string(str_of(node.get(la::NAME.code)));
    // Phase 6 (GAT projection): scope the impl's trait name so
    // `Self::Item<X>` in method body / signature resolves through
    // the trait's assoc_types directly (Self already substituted to
    // the concrete target type by current_type_params_["Self"]).
    std::string saved_impl_trait_name = current_impl_trait_name_;
    current_impl_trait_name_ = trait_name;
    struct ImplTraitNameRestore {
        std::string& dst; std::string saved;
        ~ImplTraitNameRestore() { dst = std::move(saved); }
    } _restore_itn{current_impl_trait_name_, std::move(saved_impl_trait_name)};
    // G156-1: restore current_impl_trait_args_ at impl teardown; it is SET below
    // once impl_trait_args is resolved, so lower_fn can mangle the methods by the
    // trait type-args (matching the collect-side qualified base).
    std::vector<TypeRef> saved_impl_trait_args = current_impl_trait_args_;
    current_impl_trait_args_.clear();
    struct ImplTraitArgsRestore {
        std::vector<TypeRef>& dst; std::vector<TypeRef> saved;
        ~ImplTraitArgsRestore() { dst = std::move(saved); }
    } _restore_ita{current_impl_trait_args_, std::move(saved_impl_trait_args)};
    // Push impl's own type params: either from IMPL_TYPE_PARAMS (new generic trait impl
    // form: impl<T> Trait for Struct<T>) or from TYPE_PARAMS (standalone: impl<T> Pair<T>).
    std::vector<TypeParam> impl_tps;
    if (node.has_key(la::IMPL_TYPE_PARAMS)) {
        impl_tps = read_type_params_from(node, la::IMPL_TYPE_PARAMS.code);
        push_type_params(impl_tps);
        impl_type_params_ = impl_tps;
    } else if (trait_name.empty() && node.has_key(la::TYPE_PARAMS)) {
        impl_tps = read_type_params(node);
        push_type_params(impl_tps);
        impl_type_params_ = impl_tps;  // so lower_fn includes them in fn.type_params
    }
    std::string target;
    TypeRef target_resolved = nullptr;
    if (node.has_key(la::TYPE)) {
        auto tnode = map_of(node.get(la::TYPE.code));
        if (code_of(tnode) == la::PTR_TYPE) {
            auto resolved = resolve_type(tnode);
            target = type_str(resolved);
        } else if (code_of(tnode) == la::UNSIZED_SLICE_TYPE) {
            // Phase 1B-10: parallel to sema_collect — unsized-slice impl
            // self-type. Same mangling so collection + lowering agree.
            bool was_ok = unsized_ok_;
            unsized_ok_ = true;
            auto resolved = resolve_type(tnode);
            unsized_ok_ = was_ok;
            target_resolved = resolved;
            TypeRef elem = resolved ? TypeRef(resolved).elem() : TypeRef(nullptr);
            if (elem && TypeRef(elem).kind() == LogosType::Kind::TypeVar) {
                target = "$slice$T";
            } else {
                target = "$slice$" + (elem ? type_str(elem) : std::string("?"));
            }
        } else if (code_of(tnode) == la::DYN_TYPE) {
            // Phase 1B-10: parallel to sema_collect — dyn-trait impl self-type.
            bool was_ok = unsized_ok_;
            unsized_ok_ = true;
            auto resolved = resolve_type(tnode);
            unsized_ok_ = was_ok;
            target_resolved = resolved;
            target = "$dyn$" + (resolved ? std::string(TypeRef(resolved).trait_name())
                                         : std::string("?"));
        } else if (code_of(tnode) == la::REF_TYPE ||
                   code_of(tnode) == la::MUT_REF_TYPE) {
            // Mirror sema_collect: "$ref_Foo" / "$mut_ref_Foo" (symbol-safe,
            // distinct namespace from regular structs). See sema_collect.cpp
            // for full rationale.
            auto resolved = resolve_type(tnode);
            target_resolved = resolved;
            std::string prefix = (code_of(tnode) == la::MUT_REF_TYPE) ? "$mut_ref_" : "$ref_";
            TypeRef pointee = resolved ? TypeRef(resolved).pointee() : TypeRef(nullptr);
            // `impl Trait for &[T]` / `&mut [T]` → Kind::Slice (fat ptr): mangle
            // under the same `$slice$<elem>` / `$slice$T` key as bare `[T]` and
            // bind Self to the UnsizedSlice form. MUST mirror sema_collect.cpp.
            if (resolved && TypeRef(resolved).kind() == LogosType::Kind::Slice) {
                TypeRef selem = TypeRef(resolved).elem();
                target = (selem && TypeRef(selem).kind() == LogosType::Kind::TypeVar)
                         ? std::string("$slice$T")
                         : "$slice$" + (selem ? type_str(selem) : std::string("?"));
                target_resolved = make_unsized_slice_type(selem);
            } else if (pointee && (TypeRef(pointee).kind() == LogosType::Kind::Struct ||
                            TypeRef(pointee).kind() == LogosType::Kind::ZonedStruct)) {
                bool has_tvar = false;
                for (auto a : TypeRef(pointee).type_args())
                    if (a && TypeRef(a).kind() == LogosType::Kind::TypeVar) { has_tvar = true; break; }
                if (TypeRef(pointee).type_args().empty() || has_tvar) {
                    target = prefix + std::string(TypeRef(pointee).struct_name());
                } else {
                    target = prefix + concrete_struct_name(pointee);
                }
            } else if (pointee && TypeRef(pointee).kind() == LogosType::Kind::TypeVar) {
                // Phase 1B-8: parallel to sema_collect.cpp — `impl<T> Trait
                // for &T` uses sentinel `$ref$T` / `$mut_ref$T` so lowering
                // emits the same mangled name as collection.
                target = prefix + "$T";
            } else {
                target = prefix + type_str(resolved);
            }
        } else if (code_of(tnode) == la::GENERIC_INST) {
            target = std::string(str_of(tnode.get(la::NAME.code)));
            if (impl_tps.empty()) {
                auto resolved = resolve_type(tnode);
                if (resolved && !TypeRef(resolved).type_args().empty()) {
                    bool concrete = true;
                    for (auto a : TypeRef(resolved).type_args())
                        if (a && TypeRef(a).kind() == LogosType::Kind::TypeVar) { concrete = false; break; }
                    if (concrete) {
                        if (TypeRef(resolved).kind() == LogosType::Kind::Struct ||
                            TypeRef(resolved).kind() == LogosType::Kind::ZonedStruct) {
                            target = concrete_struct_name(resolved);
                            target_resolved = resolved;
                        }
                    }
                }
            } else {
                // CP-cm-16 follow-up: parallel to sema_collect.cpp:2037 —
                // generic-target impl (`impl<T,E> ... for Foo<Vec<T>, E>`)
                // captures the full pattern with TypeVars unsubstituted so
                // mono can pattern-unify against concrete receivers and
                // bind impl-level T,E correctly (vs naive positional
                // binding which conflates Vec<i32> with T).
                target_resolved = resolve_type(tnode);
            }
        } else if (code_of(tnode) == la::TUPLE_TYPE) {
            // SL-sl-08: parallel to sema_collect.cpp — `impl Trait for
            // (A, B, …)` mangling. Generic form (TypeVar elems) →
            // `$tuple$N`; concrete form → `$tuple$N$<t1>$<t2>…`.
            // Variadic form `impl<A...> Trait for (A...)` → `$tuple$variadic`.
            auto resolved = resolve_type(tnode);
            target_resolved = resolved;
            if (resolved && TypeRef(resolved).kind() == LogosType::Kind::Void) {
                target = "void";
            } else {
                auto elems = TypeRef(resolved).tuple_elems();
                bool is_variadic_target = false;
                if (elems.size() == 1) {
                    TypeRef e0(elems[0]);
                    if (e0 && e0.kind() == LogosType::Kind::TypeVar) {
                        std::string tvn(e0.type_var_name());
                        for (auto& itp : impl_tps)
                            if (itp.name == tvn && itp.is_variadic) {
                                is_variadic_target = true;
                                break;
                            }
                    }
                }
                if (is_variadic_target) {
                    target = "$tuple$variadic";
                } else {
                    size_t arity = elems.size();
                    bool any_tvar = false;
                    for (auto e : elems)
                        if (e && TypeRef(e).kind() == LogosType::Kind::TypeVar)
                            { any_tvar = true; break; }
                    target = "$tuple$" + std::to_string(arity);
                    if (!any_tvar) {
                        for (auto e : elems) {
                            target += "$";
                            target += (e ? type_str(e) : std::string("?"));
                        }
                    }
                }
            }
        } else if (code_of(tnode) == la::FN_PTR_TYPE) {
            // G149-6: parallel to sema_collect — fn-pointer impl target keyed
            // by arity (`$fnptr$N`); type-erased to a uniform pointer.
            auto resolved = resolve_type(tnode);
            target_resolved = resolved;
            target = "$fnptr$" + std::to_string(TypeRef(resolved).closure_params().size());
        } else {
            target = std::string(str_of(tnode.get(la::NAME.code)));
            // Unfold type aliases: `type ObjectArray = Array<AnyVal>;` makes
            // `impl Trait for ObjectArray` equivalent to `impl Trait for Array<AnyVal>`.
            auto ait = type_aliases_.find(target);
            if (ait != type_aliases_.end() && ait->second.type_params.empty()) {
                auto aliased = ait->second.type;
                if (aliased && (TypeRef(aliased).kind() == LogosType::Kind::Struct ||
                                TypeRef(aliased).kind() == LogosType::Kind::ZonedStruct)) {
                    if (!TypeRef(aliased).type_args().empty()) {
                        target = concrete_struct_name(aliased);
                        target_resolved = aliased;
                    } else {
                        target = std::string(TypeRef(aliased).struct_name());
                    }
                }
            }
        }
    }
    lir::LImplBlock ib;
    ib.trait_name   = trait_name;
    ib.target_type  = target;
    // CP-cm-16 follow-up: full impl-target pattern with TypeVars unsubstituted.
    // Set for generic-target impls (`impl<T,E> ... for Foo<Vec<T>, E>`) so
    // mono can pattern-unify against the concrete receiver. Null for the
    // concrete + non-generic + primitive + special-target cases (only
    // GENERIC_INST + non-empty impl_tps populates target_resolved on
    // this path; mono falls back to positional binding when null).
    ib.target_typeref = target_resolved;
    ib.impl_type_params = impl_tps;
    // B62: copy trait-arg region info captured by collect_impl, so mono's
    // method_bound_ok can detect HRTB satisfaction mismatch.
    if (!trait_name.empty()) {
        auto it = impls_.find(trait_name + "::" + target);
        if (it != impls_.end()) {
            ib.trait_type_args      = it->second.trait_type_args;
            ib.trait_lifetime_args  = it->second.trait_lifetime_args;
            ib.impl_lifetime_params = it->second.impl_lifetime_params;
            ib.lifetime_outlives    = it->second.impl_lifetime_outlives;  // B65
            // Validate impl-level outlives names against declared impl_lifetime_params.
            std::unordered_set<std::string> declared(ib.impl_lifetime_params.begin(),
                                                     ib.impl_lifetime_params.end());
            auto known = [&](std::string_view lt) {
                if (lt.empty()) return true;
                if (lt == "'static" || lt == "static") return true;
                return declared.count(std::string(lt)) > 0;
            };
            for (auto& [lng, sht] : ib.lifetime_outlives) {
                if (!known(lng))
                    error(std::format("impl {} for {}: use of undeclared lifetime name '{}' in outlives clause",
                                      trait_name, target, lng));
                if (!known(sht))
                    error(std::format("impl {} for {}: use of undeclared lifetime name '{}' in outlives clause",
                                      trait_name, target, sht));
            }
        }
    }
    // Propagate genos type_code: `impl Varchar for HermesString` on a
    // trait that carries #[type_code=N] sets the target struct's type_code
    // (if the struct hasn't got one already).  This is how eide inherit
    // their identity from the logical datatype family.
    if (!trait_name.empty() && !target.empty()) {
        for (const auto& td : prog.traits) {
            if (td.name != trait_name || td.type_code == 0) continue;
            bool applied = false;
            for (auto& sd : prog.structs) {
                if (sd.name != target) continue;
                // Trait-declared type_code wins over the hash-derived default
                // auto-assigned at eidos lowering time.  An explicit
                // `#[type_code]` on the eidos itself would normally also
                // win, but we don't allow both at the moment.
                sd.type_code = td.type_code;
                auto fqn = cur_package_.empty() ? sd.name
                                                 : cur_package_ + "::" + sd.name;
                explicit_type_codes_[fqn] = sd.type_code;
                applied = true;
                break;
            }
            // Target not found as a concrete struct — it's a generic
            // instantiation (e.g. `impl Array for Array<AnyVal>` where
            // target = "Array$G1$AnyVal").  Record as an inst annotation
            // so mono applies the type_code to the cloned concrete struct,
            // and also register the canonical name in explicit_type_codes_
            // so sema-time queries (`type_code_of::<Array<AnyVal>>()`) hit.
            if (!applied && target.find("$G") != std::string::npos) {
                lir::LInstAnnotation ia;
                ia.mangled_name = target;
                ia.type_code    = td.type_code;
                ia.struct_type  = target_resolved;  // for mono struct demand
                // Derive canonical "pkg::BaseName<Args>" from the target type
                // (use target_resolved captured earlier — always set when the
                // target is a concrete generic instantiation, i.e. this path).
                std::string pkg;
                if (target_resolved) {
                    // Use get_*_si which handles pkg_name correctly
                    if (auto* dsi_tr = get_datatype_si(target_resolved)) pkg = dsi_tr->package;
                    else if (auto* ssi_tr = get_struct_si(target_resolved)) pkg = ssi_tr->package;
                    if (pkg.empty()) pkg = cur_package_;
                    ia.canonical_name = pkg + "::" + type_str(target_resolved);
                    explicit_type_codes_[ia.canonical_name] = td.type_code;
                }
                // Also register the mangled-form key ("pkg::Array$G1$AnyVal")
                // so the dispatch-entry emission code (which looks up by mangled
                // target) picks up the propagated type_code.
                {
                    std::string p = pkg.empty() ? std::string(cur_package_) : pkg;
                    std::string mangled_fqn = p.empty() ? target : p + "::" + target;
                    explicit_type_codes_[mangled_fqn] = td.type_code;
                }
                prog.inst_annotations.push_back(std::move(ia));
            }
            break;
        }
    }
    // Blanket impl detection: target IS one of this impl's own type parameters.
    if (!trait_name.empty()) {
        for (auto& tp : impl_tps) {
            if (tp.name == target) {
                ib.is_blanket = true;
                if (!tp.bounds.empty()) {
                    ib.bound_trait = tp.bounds[0].trait_name;
                    ib.primary_assoc_eqs = tp.bounds[0].assoc_eqs;
                    for (size_t bi = 1; bi < tp.bounds.size(); ++bi) {
                        ib.extra_bounds.push_back(tp.bounds[bi].trait_name);
                        ib.extra_assoc_eqs.emplace_back(
                            tp.bounds[bi].trait_name, tp.bounds[bi].assoc_eqs);
                    }
                }
                break;
            }
        }
    }
    if (node.has_key(la::IS_UNSAFE)) {
        AnyVal av = node.get(la::IS_UNSAFE);
        ib.is_unsafe = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }
    // Resolve trait type args and push into scope
    std::vector<TypeRef> impl_trait_args;
    if (!trait_name.empty() && node.has_key(la::TYPE_PARAMS)) {
        AnyVal tpav = node.get(la::TYPE_PARAMS.code);
        if (!tpav.is_null()) {
            auto tplist = map_of(tpav);
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                auto tit = traits_.find(trait_name);
                size_t type_arg_idx = 0;
                for (uint64_t i = 0; i < items.size(); ++i) {
                    auto item = map_of(items.get(i));
                    // L1: skip LIFETIME_PARAM at trait-arg position; Logos
                    // doesn't track regions structurally for trait dispatch.
                    if (code_of(item) == la::LIFETIME_PARAM) continue;
                    auto resolved = resolve_type(item);
                    impl_trait_args.push_back(resolved);
                    if (tit != traits_.end() && type_arg_idx < tit->second.type_params.size())
                        current_type_params_[tit->second.type_params[type_arg_idx].name] = resolved;
                    ++type_arg_idx;
                }
            }
        }
    }
    // G156-1: expose the impl's concrete trait type-args to lower_fn (mangling).
    current_impl_trait_args_ = impl_trait_args;
    // G156-1: ib.trait_type_args was seeded (line ~1316) from
    // impls_["Trait::Target"], which for two `Trait<T>` impls of one type holds
    // only the LAST-inserted impl's args (coherence map is last-wins). Override
    // with THIS impl's own resolved args so mono keys the assoc-type impls under
    // the correct `Trait$G..$Args` suffix.
    if (!impl_trait_args.empty())
        ib.trait_type_args = impl_trait_args;
    // Propagate type_code from a genos-specialization decl:
    // `#[type_code=100] pub genos Array<AnyVal>;` + `impl Array<AnyVal> for E`
    // → E inherits type_code 100.  Only fires if the direct trait didn't
    // already carry a type_code (handled above).
    if (!trait_name.empty() && !target.empty() && !impl_trait_args.empty()) {
        // Build canonical "pkg::Trait<Args>" to look up explicit_type_codes_.
        // Try both the impl site's package and any known owner of the trait.
        std::string tpkg = std::string(cur_package_);
        std::string canon = tpkg + "::" + trait_name + "<";
        for (size_t i = 0; i < impl_trait_args.size(); ++i) {
            if (i) canon += ", ";
            canon += type_str(impl_trait_args[i]);
        }
        canon += ">";
        auto eit = explicit_type_codes_.find(canon);
        if (eit != explicit_type_codes_.end()) {
            for (auto& sd : prog.structs) {
                if (sd.name != target) continue;
                sd.type_code = eit->second;
                auto fqn = cur_package_.empty() ? sd.name
                                                 : cur_package_ + "::" + sd.name;
                explicit_type_codes_[fqn] = sd.type_code;
                break;
            }
        }
    }
    // Lower impl methods as free functions (Target__method).
    // For `impl<T> GenericStruct<T>` blocks, add methods to the struct template instead of
    // prog.functions so mono's instantiate_one_struct can clone them with T substituted.
    // For `impl<V> PartialSpec<Concrete, V>` attach to the matching partial spec
    // (so mono picks up methods when instantiating the spec, not the base template).
    lir::LStructDef* target_struct_tmpl = nullptr;
    if (!impl_tps.empty()) {
        // Try matching a partial/full spec first.  The impl's target type,
        // resolved with impl_tps' TypeVars bound, should match a spec's
        // spec_patterns via pattern equality (same TypeVar positions, same
        // concrete positions).
        if (node.has_key(la::TYPE)) {
            auto tnode = map_of(node.get(la::TYPE.code));
            if (code_of(tnode) == la::GENERIC_INST) {
                auto base_name = std::string(str_of(tnode.get(la::NAME.code)));
                auto target_type = resolve_type(tnode);
                if (target_type && !TypeRef(target_type).type_args().empty()) {
                    for (auto& ss : prog.struct_specializations) {
                        if (ss.name != base_name) continue;
                        if (ss.spec_patterns.size() != TypeRef(target_type).type_args().size()) continue;
                        bool match = true;
                        for (size_t i = 0; i < ss.spec_patterns.size(); ++i) {
                            auto a = TypeRef(target_type).type_args()[i];
                            auto p = ss.spec_patterns[i];
                            if (!a || !p) { match = false; break; }
                            if (TypeRef(a).kind() == LogosType::Kind::TypeVar &&
                                TypeRef(p).kind() == LogosType::Kind::TypeVar) continue;  // both TV, OK
                            if (TypeRef(a).kind() == LogosType::Kind::TypeVar ||
                                TypeRef(p).kind() == LogosType::Kind::TypeVar) { match = false; break; }
                            if (!types_equal(a, p)) { match = false; break; }
                        }
                        if (match) { target_struct_tmpl = &ss; break; }
                    }
                }
            }
        }
        if (!target_struct_tmpl) {
            // Prefer struct in the impl's pkg (cur_package_) over a same-named
            // struct from another pkg (e.g. stdlib's Box vs user's Box).
            for (auto& sd : prog.structs)
                if (sd.name == target && sd.pkg == cur_package_) {
                    target_struct_tmpl = &sd; break;
                }
            if (!target_struct_tmpl) {
                for (auto& sd : prog.structs)
                    if (sd.name == target) { target_struct_tmpl = &sd; break; }
            }
        }
    }
    StrSet overridden;
    // Blanket impls lower methods under a synthetic target name so they don't
    // collide with `T::method` for any other generic `T` in the program.
    std::string lower_target = ib.is_blanket
        ? ("$blanket$" + trait_name + "$" + ib.bound_trait + "$" + target)
        : target;
    // Phase 1B-11: when target_resolved holds an unsized self-type kind
    // (UnsizedSlice / UnsizedDyn), seed `Self` before lower_fn so the
    // method body's `self: &Self` / `&Self` references resolve correctly.
    // For sized targets, lower_fn falls back to its own lookup_type_by_name
    // path which works on struct/datatype/primitive names.
    if (target_resolved &&
        (TypeRef(target_resolved).kind() == LogosType::Kind::UnsizedSlice ||
         TypeRef(target_resolved).kind() == LogosType::Kind::UnsizedDyn) &&
        !current_type_params_.count("Self")) {
        current_type_params_["Self"] = target_resolved;
    }
    // `impl Trait for str` falls through to the simple_type branch which
    // sets target="str" but doesn't set target_resolved. For the unsized
    // Self semantics ( so `&Self` canonicalises to Slice<u8> at lowering),
    // seed Self with UnsizedSlice<u8>.
    if (target == "str" && !current_type_params_.count("Self")) {
        LogosTypeBuilder us; us.kind = LogosType::Kind::UnsizedSlice;
        us.elem = u8_t();
        current_type_params_["Self"] = pool_->alloc(std::move(us));
    }
    // Seed `Self` for impl-target shapes whose mangled `struct_ctx` name is NOT
    // resolvable by lower_fn's name-lookup path (which only handles bare
    // struct/datatype/primitive names). Without this, `self: Self` / `&Self` /
    // `Self::…` in the method body errors "unknown type 'Self'". Set
    // UNCONDITIONALLY (overriding any stale `Self` left by a prior impl whose
    // cleanup didn't fire — an impl's Self is definitively its own target) with
    // save/restore so it doesn't leak into the next impl. Covers:
    //   - tuple / fn-ptr targets (SL-sl-08 / G149-6 — without the override a
    //     stale struct Self made `impl<A...> Trait for (A...)` resolve wrong),
    //   - reference targets `impl Trait for &T`            → Self = &T  (G156-8)
    //   - concrete-type-arg target, no impl param `impl Foo<i64>`       (G156-9)
    //   - blanket impl on a type-var `impl<…F:Bar> Trait for F` → Self = F
    //     (explicit methods; default methods are already seeded — G156-13)
    TypeRef seed_self = nullptr;
    if (target_resolved) {
        auto tk = TypeRef(target_resolved).kind();
        if (tk == LogosType::Kind::Tuple || tk == LogosType::Kind::FnPtr ||
            tk == LogosType::Kind::Ref   || tk == LogosType::Kind::MutRef)
            seed_self = target_resolved;
        else if ((tk == LogosType::Kind::Struct ||
                  tk == LogosType::Kind::ZonedStruct ||
                  tk == LogosType::Kind::Enum) &&
                 impl_tps.empty() && !TypeRef(target_resolved).type_args().empty())
            seed_self = target_resolved;  // concrete-type-arg, no impl param
    }
    if (!seed_self && ib.is_blanket && !impl_tps.empty())
        seed_self = make_typevar(target);  // blanket on a bound type-var
    bool _restore_tuple_self = false;
    TypeRef _saved_tuple_self = nullptr;
    bool _had_tuple_self = false;
    if (seed_self) {
        _had_tuple_self = current_type_params_.count("Self") > 0;
        if (_had_tuple_self) _saved_tuple_self = current_type_params_["Self"];
        current_type_params_["Self"] = seed_self;
        _restore_tuple_self = true;
    }
    // G149-6: a fn-ptr is type-erased to a uniform pointer, so its methods emit
    // ONCE non-generic — clear impl_type_params_ so lower_fn doesn't make a
    // never-instantiated generic template. (Cleared again at teardown.)
    if (target.rfind("$fnptr$", 0) == 0)
        impl_type_params_.clear();
    if (node.has_key(la::ITEMS)) {
        auto items = arr_of(node.get(la::ITEMS.code));
        // Phase A.2: doc-line sweep — pending_doc_ primes the next lower_fn.
        pending_doc_.clear();
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto m = map_of(items.get(i));
            if (try_append_doc(pending_doc_, m)) continue;
            if (code_of(m) == la::FN || code_of(m) == la::STATIC_FN) {
                auto fn = lower_fn(m, lower_target);
                // Trait-impl methods inherit visibility from the trait itself:
                // if the trait is reachable, its methods are callable (Rust
                // semantics).  The grammar does not allow `pub` on trait
                // methods, so force is_pub=true when lowering under a trait
                // impl block.  Inherent-impl methods (no trait_name) keep the
                // explicit `pub fn` / private split.
                if (!trait_name.empty()) fn.is_pub = true;
                overridden.insert(fn.name);
                // Also track base name so overloaded explicit methods block
                // their corresponding defaults (overloads share a base name).
                if (!trait_name.empty() && m.has_key(la::NAME)) {
                    auto raw = std::string(str_of(m.get(la::NAME.code)));
                    overridden.insert(lower_target + "__" + raw);
                }
                if (target_struct_tmpl) {
                    // Add to struct template so mono clones it during struct instantiation.
                    // Drop impl/struct-level type params (mono re-injects them); keep
                    // method-level ones (e.g. `fn m<H>` on an `impl<T> Trait for Foo<T>`).
                    if (!fn.type_params.empty()) {
                        std::vector<TypeParam> kept;
                        kept.reserve(fn.type_params.size());
                        for (auto& tp : fn.type_params) {
                            bool is_impl_level = false;
                            for (auto& itp : impl_tps)
                                if (itp.name == tp.name) { is_impl_level = true; break; }
                            if (!is_impl_level) kept.push_back(tp);
                        }
                        fn.type_params = std::move(kept);
                    }
                    // Preserve impl-level type params with their bounds so mono
                    // can gate instantiation on bound satisfaction.
                    fn.impl_type_params = impl_tps;
                    target_struct_tmpl->methods.push_back(std::make_unique<lir::LFunction>(std::move(fn)));
                } else {
                    // CP-cm-16 follow-up: enum-impl path (impl<T,E> Trait for
                    // Result<Vec<T>, E>). Methods go to prog.functions with
                    // impl-level T,E flattened into fn.type_params (per the
                    // existing comment at mono_clone.cpp:4319). Carry the
                    // impl-target pattern so mono's instantiate_enum_templates
                    // can unify pattern↔receiver instead of positional binding.
                    fn.impl_target_pattern = ib.target_typeref;
                    prog.functions.push_back(std::make_unique<lir::LFunction>(std::move(fn)));
                }
            } else if (code_of(m) == la::ASSOC_CONST_IMPL) {
                pending_doc_.clear();
                // g9/B121: emit a zero-arg accessor `Target__kassoc_<name>`
                // returning the const value, so a generic projection
                // `T::<name>` (lowered by try_lower_generic_assoc_const to a
                // `T__kassoc_<name>()` call that mono rewrites to
                // `Target__kassoc_<name>` once T is substituted) has a concrete
                // function to call. Only for concrete trait-impl targets;
                // generic-target / blanket assoc consts are out of scope (the
                // value could depend on the impl's type-params — a rarer
                // follow-up). The concrete `Target::<name>` read still uses the
                // direct assoc_const_impls_ value path (no accessor needed).
                if (!trait_name.empty() && impl_tps.empty() &&
                    !target_struct_tmpl && m.has_key(la::NAME) &&
                    m.has_key(la::VALUE)) {
                    auto cname = std::string(str_of(m.get(la::NAME.code)));
                    TypeRef ctype = m.has_key(la::TYPE)
                        ? resolve_type(map_of(m.get(la::TYPE.code))) : nullptr;
                    auto val = lower_expr(map_of(m.get(la::VALUE.code)));
                    if (ctype) builder().retype_expr(val, ctype);
                    lir::LFunction acc;
                    acc.name        = lower_target + "__kassoc_" + cname;
                    acc.method_base = "kassoc_" + cname;
                    acc.package     = cur_package_;
                    acc.ret_type    = ctype ? ctype
                                            : (val ? val->type : void_t());
                    acc.is_pub      = true;
                    acc.source_file = file_;
                    acc.body.stmts.push_back(builder().stmt_return(val, 0));
                    prog.functions.push_back(
                        std::make_unique<lir::LFunction>(std::move(acc)));
                }
            } else {
                // Non-fn impl item (assoc-type). Discard any sweep doc since
                // assoc-type docs are deferred to Phase A.3.
                pending_doc_.clear();
            }
        }
        pending_doc_.clear();
    }
    // Lower default methods from the trait that weren't overridden.
    if (!trait_name.empty()) {
        auto tit = traits_.find(trait_name);
        if (tit != traits_.end()) {
            for (auto& m : tit->second.methods) {
                // Blanket impls lower defaults under the synthetic `$blanket$…`
                // target (matching `lower_target` + the collect-side
                // registration) with Self = the blanket TypeVar, so the LIR
                // template `$blanket$Trait$Bound$T__method` exists for mono to
                // clone per concrete receiver. Without this, the call dispatched
                // by try_blanket_method_dispatch dangles to an empty stub.
                auto mangled = lower_target + "__" + m.name;
                if (m.has_default && !overridden.count(mangled)) {
                    // Push Self → target type; for generic impls include type params as TypeVars.
                    TypeRef self_type = nullptr;
                    if (ib.is_blanket) {
                        self_type = make_typevar(target);
                    } else {
                        auto [spkg2, ssi2] = find_struct_by_name(target);
                        auto [dpkg2, dsi2] = find_datatype_by_name(target);
                        if (ssi2) {
                            if (!impl_tps.empty()) {
                                std::vector<TypeRef> tv_args;
                                for (auto& tp : impl_tps)
                                    tv_args.push_back(make_typevar(tp.name));
                                self_type = make_generic_struct(target, std::move(tv_args), {}, spkg2);
                            } else {
                                self_type = make_struct_type(target, spkg2);
                            }
                        } else if (dsi2) {
                            if (!impl_tps.empty()) {
                                std::vector<TypeRef> tv_args;
                                for (auto& tp : impl_tps)
                                    tv_args.push_back(make_typevar(tp.name));
                                self_type = make_generic_datatype(target, std::move(tv_args), {}, dpkg2);
                            } else {
                                self_type = make_datatype_type(target, dpkg2);
                            }
                        } else if (auto prim_t = lookup_type_by_name(target)) {
                            // impl Trait for <primitive> (isize / u32 / …):
                            // bind Self to the primitive type so default-body
                            // signatures referencing `&Self` resolve.
                            self_type = prim_t;
                        }
                    }
                    if (self_type)
                        current_type_params_["Self"] = self_type;
                    // Switch holder to the zone that owns the default AST node
                    // (may be a different module's zone for cross-module traits).
                    auto* saved_holder = holder_;
                    if (m.default_holder) holder_ = m.default_holder;
                    auto fn = lower_fn(map_of(m.default_ast), lower_target);
                    holder_ = saved_holder;
                    fn.is_pub = true;  // default trait method inherits trait visibility
                    // Generic impl: the default method must travel as a struct-
                    // template method so mono clones it per concrete struct
                    // instantiation. Otherwise the bare-name template ends up as
                    // a free fn that mono only clones on explicit callsites —
                    // and dyn-trait dispatch is NOT a callsite.
                    if (target_struct_tmpl) {
                        if (!fn.type_params.empty()) {
                            std::vector<TypeParam> kept;
                            kept.reserve(fn.type_params.size());
                            for (auto& tp : fn.type_params) {
                                bool is_impl_level = false;
                                for (auto& itp : impl_tps)
                                    if (itp.name == tp.name) { is_impl_level = true; break; }
                                if (!is_impl_level) kept.push_back(tp);
                            }
                            fn.type_params = std::move(kept);
                        }
                        fn.impl_type_params = impl_tps;
                        target_struct_tmpl->methods.push_back(std::make_unique<lir::LFunction>(std::move(fn)));
                    } else {
                        // CP-cm-16 follow-up: parallel propagation for
                        // trait-default methods on enum-impl path.
                        fn.impl_target_pattern = ib.target_typeref;
                        prog.functions.push_back(std::make_unique<lir::LFunction>(std::move(fn)));
                    }
                    current_type_params_.erase("Self");
                }
            }
        }
    }
    // Restore the `Self` binding we overrode for a tuple-target impl.
    if (_restore_tuple_self) {
        if (_had_tuple_self) current_type_params_["Self"] = _saved_tuple_self;
        else current_type_params_.erase("Self");
    }
    // Clean up trait type params
    if (!trait_name.empty()) {
        auto tit = traits_.find(trait_name);
        if (tit != traits_.end()) {
            for (auto& tp : tit->second.type_params)
                current_type_params_.erase(tp.name);
        }
    }
    // Clean up impl's own type params
    if (!impl_tps.empty()) { pop_type_params(impl_tps); impl_type_params_.clear(); }
    // Copy associated type mappings.  Blanket impls register under the
    // synthetic `$blanket$...` name (see sema_collect) so the prefix must
    // reflect that to pick up the right entries.
    if (!trait_name.empty()) {
        std::string stored_target = ib.is_blanket
            ? ("$blanket$" + trait_name + "$" + ib.bound_trait + "$" + target)
            : target;
        // G156-1: assoc-type impls are registered under a trait-arg-suffixed key
        // (e.g. "Producer$G1$i64::Gen::Item"); for two `Trait<T>` impls of one
        // type the bare plain key is erased, so match THIS impl's suffixed
        // prefix. Empty suffix (non-generic trait) → bare prefix, unchanged.
        auto prefix = trait_name + trait_targ_suffix(impl_trait_args)
                    + "::" + stored_target + "::";
        for (auto& [key, entry] : assoc_type_impls_) {
            if (key.rfind(prefix, 0) == 0) {
                auto assoc_name = key.substr(prefix.size());
                ib.assoc_types[assoc_name] = entry.type;
            }
        }
    }
    ib.doc = std::move(impl_doc);
    prog.impls.push_back(std::move(ib));

    // ── Tag-dispatch: emit LDispatchEntry records ─────────────────────────
    // Conditions: trait has #[tag_dispatch(TS)], target is a concrete (non-generic)
    // datatype with a known type_code, impl is not a generic impl block.
    if (!trait_name.empty() && impl_tps.empty()) {
        std::string tag_system;
        for (auto& td : prog.traits) {
            if (td.name == trait_name) { tag_system = td.tag_dispatch_system; break; }
        }
        if (!tag_system.empty()) {
            // Prefer the type_code from prog.structs (which has annotation-applied codes,
            // e.g. from #[type_code=N]).  Fall back to hash computation if not found.
            uint64_t tcode = 0;
            for (auto& sd : prog.structs) {
                // Skip entries with type_code==0: a zero entry means the
                // annotation hasn't been applied yet (or it's an unannotated
                // type), so breaking early would prevent finding a later entry
                // with the correct code (e.g. from a different imported file).
                if (sd.is_zoned && sd.name == target && sd.type_code != 0) {
                    tcode = sd.type_code; break;
                }
            }
            if (tcode == 0) {
                // Also check explicit_type_codes_ for types annotated but not yet in prog.structs.
                // Keys are fully-qualified ("pkg::Name"). Try the current package first
                // (for same-file impls), then the target type's own package (for impls
                // on foreign types, e.g. `impl HermesEqual for HermesString` in
                // package hermes.equal — HermesString was annotated in hermes.string).
                auto target_fqn = cur_package_.empty() ? target : cur_package_ + "::" + target;
                auto eit = explicit_type_codes_.find(target_fqn);
                if (eit == explicit_type_codes_.end()) {
                    // For a generic-instantiation target ("Array$G1$AnyVal"),
                    // datatypes_ keys by the base name ("Array").  Strip the
                    // "$G..." suffix for the package lookup.
                    std::string base = target;
                    if (auto p = base.find("$G"); p != std::string::npos)
                        base = base.substr(0, p);
                    auto [bpkg, bsi] = find_datatype_by_name(base);
                    if (bsi) {
                        auto foreign_fqn = bsi->package + "::" + target;
                        eit = explicit_type_codes_.find(foreign_fqn);
                    }
                }
                if (eit != explicit_type_codes_.end()) {
                    tcode = eit->second;
                } else {
                    auto [tpkg2, tsi2] = find_datatype_by_name(target);
                    if (tsi2) {
                        std::string canon = tsi2->package + "::" + target;
                        auto hash = type_hash_23(canon);
                        uint64_t raw = type_hash_56bit(hash);
                        tcode = (raw < 128) ? (raw + 128) : raw;
                    }
                }
            }
            if (tcode != 0) {
                // Use traits_ (SemaTraitInfo) which has has_default; prog.traits (LTraitDef)
                // only has the signature, not the default-body flag.
                auto tit = traits_.find(trait_name);
                if (tit != traits_.end()) {
                    for (auto& m : tit->second.methods) {
                        // Only emit entry if the method is actually lowered.
                        // A method exists iff: explicitly overridden OR has a default body.
                        auto mangled = target + "__" + m.name;
                        if (!overridden.count(mangled) && !m.has_default) continue;

                        // Resolve the bare convention-name to the actual mangled
                        // symbol stored in funcs_ (unconditional mangling appends
                        // an `__f__sig` suffix to every fn).
                        std::string actual_sym = mangled;
                        for (auto* cand : find_func_candidates(mangled)) {
                            if (cand && !cand->symbol_name.empty()) {
                                actual_sym = cand->symbol_name;
                                break;
                            }
                        }

                        lir::LDispatchEntry de;
                        de.tag_system     = tag_system;
                        de.trait_name     = trait_name;
                        de.method_name    = m.name;
                        de.fn_symbol      = std::move(actual_sym);
                        de.impl_type_name = target;
                        de.type_code      = tcode;
                        prog.dispatch_entries.push_back(std::move(de));
                    }
                }
            }
        }
    }
}
} // namespace logos::compiler
