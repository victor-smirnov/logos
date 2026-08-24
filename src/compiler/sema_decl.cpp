// Logos project — https://github.com/victor-smirnov/logos

#include "sema_impl.hpp"

#include <logos/writ/external_ref.hpp>   // global_arena_pool, lookup_export, ExternalRef

#include <cstdio>
#include <format>
#include <functional>
#include <unordered_set>

namespace logos::compiler {

namespace la = ast;
using writ::TinyMapView;
using writ::ArrayView;
using writ::StringView;
using writ::AnyVal;
using writ::MemHolder;

// Declaration lowering methods

void SemaChecker::compute_fn_lifetime_outlives(
        TinyMapView node,
        std::string_view fn_name,
        const std::vector<std::string>& lifetime_params,
        const std::vector<lir::LParam>& params,
        TypeRef ret_type,
        std::vector<TypeParam>& type_params,
        std::vector<std::pair<std::string, std::string>>& lifetime_outlives) {
    std::unordered_set<std::string> fn_lts(lifetime_params.begin(),
                                           lifetime_params.end());
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
    lifetime_outlives = read_lifetime_outlives(node);
    for (auto& p : params) walk_implied(p.type, "", lifetime_outlives, walk_implied);
    walk_implied(ret_type, "", lifetime_outlives, walk_implied);
    auto where_outlives = read_lifetime_outlives_from(node, la::WHERE.code);
    for (auto& p : where_outlives) lifetime_outlives.push_back(std::move(p));
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
                    for (auto& tp : type_params)
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
    std::unordered_set<std::string> declared(lifetime_params.begin(),
                                             lifetime_params.end());
    auto known = [&](std::string_view lt) {
        if (lt.empty()) return true;
        if (lt == "'static" || lt == "static") return true;
        return declared.count(std::string(lt)) > 0;
    };
    for (auto& [lng, sht] : lifetime_outlives) {
        if (!known(lng))
            error(std::format("fn '{}': use of undeclared lifetime name '{}' in outlives clause",
                              fn_name, lng));
        if (!known(sht))
            error(std::format("fn '{}': use of undeclared lifetime name '{}' in outlives clause",
                              fn_name, sht));
    }
    for (auto& tp : type_params) {
        for (auto& lt : tp.lifetime_outlives) {
            if (!known(lt))
                error(std::format("fn '{}': use of undeclared lifetime name '{}' in `{}: {}` bound",
                                  fn_name, lt, tp.name, lt));
        }
    }
}

DeclBuilder SemaChecker::lower_fn(TinyMapView node, std::string_view struct_ctx,
                                  std::vector<TypeParam>* out_type_params) {
    namespace dk = lir_schema::decl_keys;
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

    // Direct-build the Func decl mirror STRAIGHT into the program WritCtr — no
    // heap accumulator. A few working locals hold the values the lowering logic
    // genuinely reads back during construction (name / type_params / ret_type /
    // params / a handful of flags); everything else is emitted directly the
    // moment it becomes final. Field→KEY map matches the former emit_fn_decl.
    DeclBuilder fn(*cur_prog_, lir_schema::decl::Code::Func, /*cap=*/40);
    std::string fn_name = mangled;                 // read pervasively below
    if (cur_from_binary_) fn.flag(dk::FROM_BINARY_MODULE, true);
    if (cur_from_lazy_)   fn.flag(dk::FROM_LAZY_MODULE, true);
    fn.str(dk::DOC, take_pending_doc());
    // Phase #[test] attributes. Consume here so they don't leak into the
    // next fn lowered in the same item-loop iteration.
    if (pending_is_test_)      fn.flag(dk::IS_TEST, true);
    if (pending_should_panic_) fn.flag(dk::SHOULD_PANIC, true);
    if (pending_ignore_)       fn.flag(dk::IGNORED, true);
    fn.str(dk::SHOULD_PANIC_MSG, pending_should_panic_expected_);
    pending_is_test_      = false;
    pending_should_panic_ = false;
    pending_ignore_       = false;
    pending_should_panic_expected_.clear();
    int32_t node_code = code_of(node);
    bool is_extern = (node_code == la::EXTERN_FN);
    if (is_extern) fn.flag(dk::IS_EXTERN, true);

    // Check is_vararg for extern fn with variadic params
    bool is_vararg = false;
    if (is_extern && node.has_key(la::IS_VARARG)) {
        AnyVal av = node.get(la::IS_VARARG.code);
        is_vararg = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }
    if (is_vararg) fn.flag(dk::IS_VARARG, true);

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

        // Bound-discriminated impl twins (`impl<T: Copy+Fst> S<T>` vs
        // `impl<T: ?Sized> S<T>`) declare STRUCTURALLY IDENTICAL methods —
        // only the bound sets differ. Prefer the candidate whose bound
        // fingerprint equals this impl's, else the loops below first-win the
        // wrong twin and the body type-checks under the wrong family.
        {
            auto bounds_fp = [](const std::vector<TypeParam>& tps) {
                std::vector<std::string> v;
                for (auto& tp : tps)
                    for (auto& b : tp.bounds) v.push_back(b.trait_name);
                std::sort(v.begin(), v.end());
                return v;
            };
            auto want_fp = bounds_fp(impl_type_params_);
            {
                auto nf = bounds_fp(node_tparams);
                want_fp.insert(want_fp.end(), nf.begin(), nf.end());
                std::sort(want_fp.begin(), want_fp.end());
            }
            size_t want_tps = impl_type_params_.size() + node_tparams.size();
            // The pass exists ONLY for bound-discriminated twins. Gate on
            // their presence (some candidate with matching type-param arity
            // carries bounds), or it steals unrelated same-name overloads
            // (static `fn new()` vs method `fn new(&self)` — the +1 self-slot
            // allowance made them ambiguous; a generic decl vs its
            // non-generic overload — the arity check above).
            bool bound_twins = !want_fp.empty();
            if (!bound_twins)
                for (auto* cand : find_func_candidates(mangled)) {
                    if (!cand || cand->type_params.size() != want_tps) continue;
                    if (!bounds_fp(cand->type_params).empty())
                        { bound_twins = true; break; }
                }
            if (bound_twins)
            for (int round = 0; round < 2 && !fi_ptr; ++round)
            for (auto* cand : find_func_candidates(mangled)) {
                if (!cand) continue;
                if (cand->type_params.size() != want_tps) continue;
                // exact param arity first; the +1 self-slot allowance only
                // as a second round so it can't shadow an exact match.
                size_t want_np = decl_param_types.size() + (round ? 1 : 0);
                if (cand->param_types.size() != want_np) continue;
                if (bounds_fp(cand->type_params) != want_fp) continue;
                size_t off = round ? 1 : 0;
                bool same = true;
                for (size_t i = 0; i < decl_param_types.size(); ++i) {
                    if (!cand->param_types[i + off] || !decl_param_types[i] ||
                        !types_equal(cand->param_types[i + off],
                                     decl_param_types[i])) { same = false; break; }
                }
                if (same) { fi_ptr = const_cast<SemaFuncInfo*>(cand); break; }
            }
        }
        // Match by (type_params arity, param signature). When this declaration
        // has type params, a non-generic same-name overload must NOT win — both
        // can have empty value-param lists (e.g. `fn f() -> u64` vs
        // `fn f<T...>() -> u64`) and only the type-params arity disambiguates.
        if (!fi_ptr)
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
            if (auto fit = find_func_by_base_and_signature(mangled, decl_param_types, is_vararg))
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
    if (!fi_ptr) {            // shouldn't happen after collect
        fn.str_always(dk::NAME, fn_name);
        return fn;
    }

    fn_name = fi_ptr->symbol_name.empty() ? mangled : fi_ptr->symbol_name;
    fn.str(dk::METHOD_BASE, std::string(raw_name));
    fn.str(dk::PKG, fi_ptr->package);
    fn.str(dk::SOURCE_FILE, fi_ptr->source_file);
    fn.str(dk::UNIT_KEY, fi_ptr->unit_key);   // UnitGraph §1.2
    // Working type_params: read by check_unique_names + compute_fn_lifetime_
    // outlives + pop_type_params below + (impl-method) caller filtering.
    std::vector<TypeParam> type_params = fi_ptr->type_params;
    std::vector<std::string> lifetime_params = read_lifetime_params(node);
    // Lifetime-param uniqueness on fn (closes B-gn-02)
    check_unique_names(lifetime_params,
                       [](auto& lt) -> std::string_view { return lt; },
                       "lifetime parameter", "fn " + mangled);
    if (!lifetime_params.empty()) {
        auto a = fn.array(dk::LIFETIME_PARAMS);
        for (auto& s : lifetime_params) a.push_str(s);
    }
    // Robust associated type resolution: call subst_type_sema even if subst is empty
    // to simplify concrete AssocType nodes (e.g. i32::Item -> bool).
    TypeRef ret_type = subst_type_sema(fi_ptr->ret_type, {});
    ret_type_      = ret_type;
    // Working param list: built incrementally (self detection, variadic, tuple/
    // pattern/mut desugar) then emitted to PARAMS at the end.
    std::vector<lir::LParam> params;
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
        auto dn = datanode_name(ret_type);
        if (!dn.empty()) {
            error(std::format(
                "return type '{}' is a DataNode eidos — cannot be returned by value. "
                "Return DataRef<{}> instead.", dn, dn));
        }
    }
    // Zone Step 4 (pin): a `#[rel_ptr]`-containing type may never cross a function
    // boundary BY VALUE — a by-value parameter is a callee stack slot, and a
    // by-value return materialises the anchored value in a register / caller
    // temporary; both invalidate the self-relative anchor (offset = target−&field).
    // Pass / return a pointer instead (`*mut T` / `&T`), which lives in the zone's
    // segment and is NOT flagged by contains_rel_ptr_field (it follows only inline
    // storage). Mirrors the DataNode "cannot be returned by value" rule above.
    if (ret_type && is_non_movable_type(ret_type))
        error(std::format(
            "return type `{}` is location-anchored (a self-relative `#[rel_ptr]` "
            "field, or a `#[pinned]` type) — it cannot be returned by value; "
            "return a pointer (`*mut {}` / `&{}`) into its zone's segment instead",
            type_str(ret_type), type_str(ret_type), type_str(ret_type)));
    // (param check is below, once params is populated)
    // Reset impl-trait inference state for this function.
    if (ret_type && TypeRef(ret_type).kind() == LogosType::Kind::ImplTrait)
        impl_ret_type_inferred_ = nullptr;

    // Put type params in scope for the duration of the function body
    push_type_params(type_params);
    // Per-method `where T: Trait` TRAIT bounds → the body scope. The subject
    // may be an IMPL-level param (not in this fn's own type_params), so they
    // can't ride tp.bounds; they augment current_type_bounds_ for the body
    // only (unwound after the body). This is what lets a method of
    // `impl<T: ?Sized> PkdArray<T>` say `where T: EntryBytes` and call
    // `src.entry_ptr()` on a `&T` receiver.
    std::vector<std::pair<std::string, TraitBound>> where_scope_bounds;
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
                    for (uint64_t j = 0; j < inner.size(); ++j) {
                        auto inode = map_of(inner.get(j));
                        if (code_of(inode) != la::TRAIT_BOUND) continue;
                        TraitBound tb;
                        tb.trait_name = std::string(str_of(inode.get(la::NAME.code)));
                        where_scope_bounds.emplace_back(tname, std::move(tb));
                    }
                }
            }
        }
    }
    std::vector<std::pair<std::string, std::vector<TraitBound>>> where_saved_bounds;
    for (auto& [wn, wb] : where_scope_bounds) {
        auto it = current_type_bounds_.find(wn);
        where_saved_bounds.emplace_back(
            wn, it != current_type_bounds_.end() ? it->second
                                                 : std::vector<TraitBound>{});
        current_type_bounds_[wn].push_back(wb);
    }
    // Type-param uniqueness on fn (B-gn-01 family)
    check_unique_names(type_params,
                       [](auto& tp) -> std::string_view { return tp.name; },
                       "type parameter", "fn " + mangled);

    scope_.clear();
    next_slot_ = 0;   // Phase-1: dense var slots are per-function.
    push_scope();
    // Reset move-tracking at each function boundary. Without this, a synthetic
    // variable name reused across functions — notably format!'s `__buf` — keeps
    // its moved state from a previous function, so the second function using
    // `format!` reports a spurious "use of moved variable '__buf'". User-named
    // locals never leaked (distinct scopes/names), which masked this.
    moved_vars_.clear();
    body_ever_moved_.clear();  // §7.1: reset ever-moved per fn (consulted in param-drop)
    // #118: drop-flag bookkeeping is per-function too. A leftover pending
    // `let` would be spliced into an unrelated body, and a leftover clear-log
    // entry would suppress a needed clear.
    pending_frame_lets_.clear();
    flag_clear_log_.clear();
    closure_drop_group_.clear();  // capture-drop groups are per-fn (name-keyed)
    capture_owner_.clear();
    pending_closure_capture_drops_.clear();
    decl_uninit_vars_.clear();  // B8: reset declared-uninit tracking per fn
    currently_uninit_vars_.clear();  // logos-core 2.7: reset definite-assignment tracker per fn

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

    // logos-core 4.5: pattern fn-params (struct + slice). Track synth-
    // name + pattern node; lower_fn emits the destructure body-prologue.
    struct PatFnParam {
        std::string synth;
        TypeRef     ty;
        TinyMapView pat_node;
    };
    std::vector<PatFnParam> fn_pat_params;

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

                    // logos-core 4.5: struct / slice pattern fn-param —
                    // `Point { x, y }: Point` or `[head, tail]: [i32; 2]`.
                    // Synth a name + register field/index bindings via
                    // body-prologue lets. Refutable shapes rejected here;
                    // irrefutable struct (every named field has a binding)
                    // and irrefutable slice (no PAT_REST or arity match)
                    // pass through.
                    if (p.has_key(la::PAT)) {
                        auto pav = p.get(la::PAT.code);
                        if (!pav.is_null() && pav.is_pointer()) {
                            auto pnode = map_of(pav);
                            int32_t pcode = code_of(pnode);
                            std::string synth = std::format(
                                "__pat_param_{}__{}", mangled, i);
                            define(synth, pt);
                            if (pcode == la::PAT_STRUCT &&
                                pnode.has_key(la::ITEMS) &&
                                TypeRef(pt).kind() == LogosType::Kind::Struct) {
                                auto items_av = pnode.get(la::ITEMS.code);
                                if (items_av.is_pointer()) {
                                    auto fm = map_of(items_av);
                                    if (fm.has_key(la::ITEMS)) {
                                        auto farr = arr_of(fm.get(la::ITEMS.code));
                                        std::string sname(TypeRef(pt).struct_name());
                                        auto [_spkg, sinfo] = find_struct_by_name(sname);
                                        for (uint64_t k = 0; k < farr.size(); ++k) {
                                            auto fnode = map_of(farr.get(k));
                                            if (code_of(fnode) == la::PAT_REST) continue;
                                            if (!fnode.has_key(la::NAME)) continue;
                                            std::string fname(str_of(fnode.get(la::NAME.code)));
                                            std::string bname = fname;
                                            if (fnode.has_key(la::VALUE)) {
                                                auto sub = map_of(fnode.get(la::VALUE.code));
                                                if (code_of(sub) == la::PAT_WILD &&
                                                    sub.has_key(la::NAME))
                                                    bname = std::string(str_of(sub.get(la::NAME.code)));
                                            }
                                            TypeRef ftype = error_t();
                                            if (sinfo)
                                                for (auto& f : sinfo->fields)
                                                    if (f.name == fname) { ftype = f.type; break; }
                                            if (bname != "_") define(bname, ftype);
                                        }
                                    }
                                }
                            }
                            params.push_back({synth, pt, false});
                            // The actual body-prologue let synthesis happens
                            // at lower_fn time (see fn_pat_params below).
                            fn_pat_params.push_back({synth, pt, pnode});
                            continue;
                        }
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
                                params.push_back({synth, pt, false});
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
                        // params, so the prologue's var_ref(synth) resolves.
                        define(std::string(pname), pt, /*is_mut=*/true);  // body sees mutable local
                        fn_mut_params.push_back({std::string(pname), synth, pt});
                        params.push_back({synth, pt, false});
                        continue;
                    }
                    define(pname, pt);
                    // A by-value `Box<dyn Trait>` param collapses to bare
                    // TraitObject in resolve_type but the callee OWNS the box:
                    // tag the binding owning_dyn so collect_drops frees it
                    // (vtable[0] drop_in_place + dealloc data + dealloc handle),
                    // and mark the LParam owning_box_dyn so call sites coerce the
                    // arg to a HEAP fat handle (matching the callee's free()).
                    // An owning `Box<dyn>` param now resolves to an OWNING
                    // TraitObject (owning bit on the type) — detect via the
                    // type, not the written name.
                    bool p_owning_box_dyn = false;
                    if (pt && TypeRef(pt).owning_trait_object()) {
                        if (auto vit = scope_.back().vars.find(std::string(pname));
                            vit != scope_.back().vars.end())
                            vit->second.owning_dyn = true;
                        p_owning_box_dyn = true;
                    }
                    params.push_back({std::string(pname), pt, p_variadic, p_owning_box_dyn});
                    params.back().slot = lookup_slot(pname);  // Phase-1
                }
            }
        }
    }
    // Param-name uniqueness (closes B-fn-02)
    check_unique_names(params,
                       [](auto& p) -> std::string_view { return p.name; },
                       "parameter", "fn " + mangled);
    // (Zone Step 4 pin: a by-value `#[rel_ptr]`-containing parameter is rejected in
    // define() — each param is registered there during the prologue below.)

    // T2-19 (E0106): lifetime-elision well-formedness on the SIGNATURE.
    // Rust's rules: an elided output reference lifetime is filled from the
    // inputs only when (rule 2) there is EXACTLY ONE input lifetime position,
    // or (rule 3) there is a `&self`/`&mut self` receiver. Otherwise the output
    // lifetime is undeterminable → E0106 "missing lifetime specifier". Logos
    // previously accepted `fn h(a:&i32,b:&i32)->&i32` (ambiguous) and
    // `fn g()->&i32` (no source). Only fire on UNANNOTATED output refs — an
    // explicit `&'a`/`&'static` is the user's choice.
    {
        // An elided output reference is one STRUCTURALLY in the return type:
        // `&T`, `&&T`, `(&T, &U)`, `[&T; N]`, `&[T]`. A ref nested inside a
        // generic type-ARG (`FilterIter<Self, &T>` where the iterator's `Item`
        // resolves to `&T`) is NOT an elided output lifetime — it's the type
        // param's own lifetime, carried by `Self`/the bound — so we do NOT
        // recurse into type_args (that was an over-broad false positive).
        std::function<bool(TypeRef)> has_elided_ref = [&](TypeRef t) -> bool {
            if (!t) return false;
            auto k = TypeRef(t).kind();
            if ((k == LogosType::Kind::Ref || k == LogosType::Kind::MutRef) &&
                TypeRef(t).lifetime().empty())
                return true;
            if (TypeRef(t).pointee() && has_elided_ref(TypeRef(t).pointee())) return true;
            if (TypeRef(t).elem() && has_elided_ref(TypeRef(t).elem())) return true;
            for (auto e : TypeRef(t).tuple_elems()) if (has_elided_ref(e)) return true;
            return false;
        };
        std::function<int(TypeRef)> count_ref_positions = [&](TypeRef t) -> int {
            if (!t) return 0;
            auto k = TypeRef(t).kind();
            int n = 0;
            if (k == LogosType::Kind::Ref || k == LogosType::Kind::MutRef) {
                n += 1;
                n += count_ref_positions(TypeRef(t).pointee());
                return n;
            }
            if (TypeRef(t).pointee()) n += count_ref_positions(TypeRef(t).pointee());
            if (TypeRef(t).elem())    n += count_ref_positions(TypeRef(t).elem());
            for (auto a : TypeRef(t).type_args())   n += count_ref_positions(a);
            for (auto e : TypeRef(t).tuple_elems()) n += count_ref_positions(e);
            return n;
        };
        if (ret_type && has_elided_ref(ret_type)) {
            int input_lts = 0;
            bool has_self_ref = false;
            for (auto& p : params) {
                if (!p.type) continue;
                input_lts += count_ref_positions(p.type);
                if (p.name == "self") {
                    auto pk = TypeRef(p.type).kind();
                    if (pk == LogosType::Kind::Ref || pk == LogosType::Kind::MutRef)
                        has_self_ref = true;
                }
            }
            // Fire on the AMBIGUOUS case: 2+ input lifetimes and no `&self`, so
            // elision rule 2/3 can't pick a source. (The 0-input case — a ref
            // returned from no borrowed input, e.g. `Box::leak` returning
            // `&'static` — is left to explicit annotation / the dangling-borrow
            // check, to avoid flagging legitimate `'static`-source functions.)
            if (!has_self_ref && input_lts >= 2) {
                error("missing lifetime specifier (E0106): this function's return "
                      "type contains a borrowed value with an elided lifetime, but "
                      "the signature has more than one input lifetime and no `&self` "
                      "— annotate which input the result borrows from (e.g. `&'a`)");
            }
        }
    }

    // B65: outlives bounds — capture explicit + implied + where-clause +
    // type-outlives. Placed AFTER params + ret_type so the implied-
    // bounds walker sees the resolved signature.
    std::vector<std::pair<std::string, std::string>> lifetime_outlives;
    compute_fn_lifetime_outlives(node, fn_name, lifetime_params, params,
                                 ret_type, type_params, lifetime_outlives);
    if (!lifetime_outlives.empty()) {
        auto a = fn.array(dk::LIFETIME_OUTLIVES);
        for (auto& pr : lifetime_outlives) { a.push_str(pr.first); a.push_str(pr.second); }
    }
    current_outlives_ = lifetime_outlives;  // B64/B65: visible to coercion sites

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
    // Don't stub metacall thunks ("<metaprog-thunk>") or user emit_source
    // chunks ("<metaprog>") — those need to JIT-compile so the discovery
    // loop can invoke them. The final non-metaprog sema pass lowers
    // everything for real.
    bool is_synth_blob = file_ == "<metaprog-blob-subst>"
                      || file_ == "<test_main_synth>"
                      // --gen-dir: blob-subst chunks renamed to their dump
                      // file keep the same discovery-pass stub semantics.
                      || file_.ends_with(".gen.logos");
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
    // ⚠ THE THUNK IS THE MECHANISM, SO IT CAN NEVER BE STUBBED.
    // A `<metaprog-thunk>` chunk exists for exactly one purpose: to be
    // JIT-compiled and called so a pending item metacall can be dispatched.
    // Stubbing its body does not defer work — it makes the fixpoint UNABLE to
    // advance: the item stays pending forever and the round after reports the
    // emitted item's consumers as unknown names.
    //
    // The comment above already states the intent ("Don't stub metacall
    // thunks"), but it was expressed only by is_synth_blob not matching
    // `<metaprog-thunk>` — and `cur_ast_has_pending_item_mc_` overrides that
    // the moment the thunk imports a package that has a pending item metacall,
    // which is exactly what a thunk for a cross-package callee does. MEASURED:
    // the two halves of a bootstrap cycle stopped compiling the moment the
    // thunk carried its callee's `use`. An intent expressed only as an absence
    // is not a rule; this is the rule.
    bool is_thunk_chunk = (file_ == "<metaprog-thunk>");
    bool skip_body = metaprog_mode_
                  && !is_thunk_chunk
                  && (cur_ast_idx_ == metaprog_entry_ast_idx_ || is_synth_blob
                      || cur_ast_has_pending_item_mc_)
                  // Methods too when the impl lives in a SYNTH chunk: a later
                  // meta-slice compile (harvest re-dispatch) sees generated
                  // families whose method bodies call sibling FREE fns — and
                  // stubbed free fns are ERASED from the meta program
                  // (run_metaprog_dispatch), so a lowered method body would
                  // hold a dangling func.call. metaprog_keep still exempts
                  // anything compile-time evaluation actually invokes.
                  && (struct_ctx.empty() || impl_target_unresolved
                      || is_synth_blob)
                  && !fn_is_metaprog_handler(fn_name)
                  && !fn_is_metaprog_keep(fn_name);
    if (skip_body) fn.flag(dk::IS_METAPROG_STUB, true);

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
    // The membership test uses the QUALIFIED link name for method-shaped
    // symbols: archives define methods as `<module_id>..<pkg>.Owner__m__sig`
    // (sym::link_name adds the prefix at emission) while the sema-side
    // fn_name has no module prefix. Testing the qualified form keeps this
    // gate in LOCKSTEP with mlir_gen's is_binary_skip (same name form, same
    // set) — the bare-ALIAS bridge this replaces desynced the two: sema
    // skipped bodies mlir_gen then couldn't match (gap #2, static assoc
    // fns), and the `$M` guard that "fixed" it matched zero real symbols,
    // silently disabling skeleton-skip and re-lowering the whole stdlib in
    // every consumer compile (2x full-suite regression).
    bool skel_skip_body = cur_from_binary_
                       && !is_extern
                       && !fn_is_metaprog_handler(fn_name)
                       && !fn_is_metaprog_keep(fn_name)
                       && binary_symbols_
                       && (binary_symbols_->count(fn_name) > 0
                           || (!cur_module_id_.empty()
                               && binary_symbols_->count(
                                      cur_module_id_ + ".." + fn_name) > 0));
    if (skel_skip_body) {
        skip_body = true;
        ++skel_skip_count_;
    } else if (cur_from_binary_ && !is_extern && binary_symbols_
               && ::getenv("LOGOS_TRACE_SKEL_MISS")) {
        std::fprintf(stderr, "[skel-miss] '%s' not in binary_symbols\n",
                     fn_name.c_str());
    }

    // Precompile-generics (Phase 5.B revival): a from_binary GENERIC template's
    // body is published in the dep's LIR blob — only its INSTANTIATIONS land in
    // the .o, not the template, so skel_skip_body (gated on binary_symbols) never
    // catches it and it would be re-lowered AST→LIR in every consumer. If the
    // published body is registered (loader registered the dep's LIR arena), route
    // mono to it cross-arena via body_external_ref and skip re-lowering. The
    // SIGNATURE is still lowered above (mono needs param/return types); only the
    // body block is replaced by the external ref.
    if (cur_from_binary_ && !is_extern && !skip_body && !type_params.empty()
        && !fn_is_metaprog_handler(fn_name) && !fn_is_metaprog_keep(fn_name)) {
        auto look = writ::global_arena_pool().lookup_export(fn_name);
        if (look.ok()) {
            fn.ext_ref(dk::BODY_EXTERNAL_REF,
                       writ::ExternalRef{look.arena_id, look.obj_id});
            skip_body = true;
            ++tmpl_ext_ref_count_;
        }
    }

    // Body (extern fns have no body)
    lir_view::BlockRef body;
    if (!is_extern && !skip_body && node.has_key(la::BODY)) {
        auto body_node = map_of(node.get(la::BODY.code));
        // Detect if the last stmt in the function body is a match.
        // If so, set the flag so lower_match treats EXPR arms as return values.
        if (ret_type && TypeRef(ret_type).kind() != LogosType::Kind::Void) {
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
        body = lower_block(body_node);
        tail_as_return_ = saved_tail_as_return;
        match_in_tail_position_ = false;
        // P4-pm-19: prepend `let user_k = synth.k;` for each tuple-
        // destructure parameter so body sees the user-visible names.
        if (!fn_tuple_params.empty()) {
            std::vector<lir_view::StmtRef> prologue;
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
            body.each_stmt([&](lir_view::StmtRef s){ prologue.push_back(s); });
            body = lir_mirror_block(*cur_prog_, prologue);
        }
        // logos-core 4.5: pattern fn-params (struct + slice) — prepend
        // a destructure-let for each binding so the body sees the user-
        // visible names. PAT_STRUCT bindings = field-read of the synth;
        // PAT_SLICE bindings = index-read of the synth.
        if (!fn_pat_params.empty()) {
            std::vector<lir_view::StmtRef> prologue;
            for (auto& pp : fn_pat_params) {
                int32_t pcode = code_of(pp.pat_node);
                if (pcode == la::PAT_STRUCT &&
                    pp.pat_node.has_key(la::ITEMS) &&
                    TypeRef(pp.ty).kind() == LogosType::Kind::Struct) {
                    auto items_av = pp.pat_node.get(la::ITEMS.code);
                    if (!items_av.is_pointer()) continue;
                    auto fm = map_of(items_av);
                    if (!fm.has_key(la::ITEMS)) continue;
                    auto farr = arr_of(fm.get(la::ITEMS.code));
                    std::string sname(TypeRef(pp.ty).struct_name());
                    auto [_spkg, sinfo] = find_struct_by_name(sname);
                    for (uint64_t k = 0; k < farr.size(); ++k) {
                        auto fnode = map_of(farr.get(k));
                        if (code_of(fnode) == la::PAT_REST) continue;
                        if (!fnode.has_key(la::NAME)) continue;
                        std::string fname(str_of(fnode.get(la::NAME.code)));
                        std::string bname = fname;
                        if (fnode.has_key(la::VALUE)) {
                            auto sub = map_of(fnode.get(la::VALUE.code));
                            if (code_of(sub) == la::PAT_WILD &&
                                sub.has_key(la::NAME))
                                bname = std::string(str_of(sub.get(la::NAME.code)));
                        }
                        if (bname == "_") continue;
                        TypeRef ftype = error_t();
                        if (sinfo)
                            for (auto& f : sinfo->fields)
                                if (f.name == fname) { ftype = f.type; break; }
                        lir::SLet sl;
                        sl.name   = bname;
                        sl.type   = ftype;
                        sl.is_mut = false;
                        sl.value  = builder().field_read(
                            builder().var_ref(pp.synth, pp.ty), fname, ftype);
                        prologue.push_back(make_stmt_emit(node_line_, std::move(sl)));
                    }
                }
                // PAT_SLICE: TODO — needs full slice-pattern destructure
                // with index reads + rest handling; deferred to a focused
                // follow-up alongside the §4.3 multi-level binding work.
            }
            body.each_stmt([&](lir_view::StmtRef s){ prologue.push_back(s); });
            body = lir_mirror_block(*cur_prog_, prologue);
        }
        // `mut x: T` params — prepend `let mut x = synth;` so the body's
        // mutable local is materialized from the (immutable) synth parameter.
        if (!fn_mut_params.empty()) {
            std::vector<lir_view::StmtRef> prologue;
            for (auto& mp : fn_mut_params) {
                lir::SLet sl;
                sl.name   = mp.user;
                sl.type   = mp.ty;
                sl.is_mut = true;
                sl.value  = builder().var_ref(mp.synth, mp.ty);
                prologue.push_back(make_stmt_emit(node_line_, std::move(sl)));
            }
            body.each_stmt([&](lir_view::StmtRef s){ prologue.push_back(s); });
            body = lir_mirror_block(*cur_prog_, prologue);
        }
        // Resolve impl Trait return type to the concrete type inferred from returns.
        if (ret_type && TypeRef(ret_type).kind() == LogosType::Kind::ImplTrait) {
            if (impl_ret_type_inferred_) {
                ret_type       = impl_ret_type_inferred_;
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
        if (ret_type && TypeRef(ret_type).kind() != LogosType::Kind::Void &&
            TypeRef(ret_type).kind() != LogosType::Kind::Error &&
            !block_always_returns(body_node)) {
            error("not all paths return a value");
        }
        tail_as_return_ = saved_check_flag;
        // §7.1 / Rust-conformance: by-value fn params (move-type, droppable)
        // must drop at the function epilogue, just like a `let` binding. The
        // body block's normal-exit collect_drops only walks the INNER scope
        // (the body's own locals); params live in the OUTER (params) scope
        // pushed by lower_fn. A body with an EXPLICIT `return` already drops
        // params via collect_all_drops (walks all scopes), but a void fn
        // (or any fn falling off the end) misses them. Emit drops for the
        // params scope here so `fn consume(_x: Move) {}` correctly drops _x.
        std::vector<lir_view::StmtRef> body_stmts;
        body.each_stmt([&](lir_view::StmtRef s){ body_stmts.push_back(s); });
        bool body_terminated = false;
        if (!body_stmts.empty()) {
            auto br = stmt_ref_of(body_stmts.back());
            if (br) {
                auto k = br.kind();
                body_terminated = (k == lir_schema::stmt::Code::Return ||
                                   k == lir_schema::stmt::Code::Break ||
                                   k == lir_schema::stmt::Code::Continue);
            }
        }
        if (!body_terminated) {
            // §7.1 follow-up: a param that was EVER moved (on any branch) is
            // a conditional-move shape — emitting a static drop at the merge
            // would re-free on the move-path. Conservative skip (sound, may
            // leak on the non-move path). Proper fix = B8-style drop-flag
            // elaboration extended to params.
            auto& frame = scope_.back();
            std::vector<lir_view::StmtRef> epilogue_drops;
            emit_frame_drops(frame, epilogue_drops, &body_ever_moved_);
            for (auto& d : epilogue_drops)
                body_stmts.push_back(std::move(d));
            body = lir_mirror_block(*cur_prog_, body_stmts);
        }
        body_ever_moved_.clear();
    }

    inside_unsafe_ = was_unsafe;
    pop_scope();
    // Unwind the per-method where bounds (reverse order restores nesting).
    for (auto it = where_saved_bounds.rbegin(); it != where_saved_bounds.rend(); ++it) {
        if (it->second.empty()) current_type_bounds_.erase(it->first);
        else current_type_bounds_[it->first] = it->second;
    }
    pop_type_params(type_params);

    // Emit the values held in working locals into the mirror, now final.
    fn.str_always(dk::NAME, fn_name);
    // Carry the source-level `pub` visibility from the AST onto the LIR decl
    // mirror so FunctionView::is_pub() is meaningful for BOTH free fns and
    // methods (previously only trait methods/accessors set it, so a `pub fn`'s
    // decl always read private). The `--emit-abi` pub-allowlist sidecar relies
    // on this to scope the ABI spec to the public surface; trait methods still
    // force pub downstream (idempotent). Set only when true so the flag stays
    // sparse (default-absent == private).
    if (node.has_key(la::IS_PUB)) {
        AnyVal pv = node.get(la::IS_PUB.code);
        if (!pv.is_null() && pv.is_value() && pv.as_value<uint8_t>() != 0)
            fn.flag(dk::IS_PUB, true);
    }
    // Mark compiler-invoked metaprog macro hooks (#[fn_macro]/#[token_macro]).
    // These are discovered by attribute and called by the metaprog driver, NOT a
    // linkable consumer API — the `--emit-abi` pub-allowlist sidecar excludes
    // them so their churning signatures (wql/trama) don't falsely trip the
    // abi-gate as ABI-breaking. Source of truth = the collected SemaFuncInfo
    // (fi_ptr), which carries the attribute flags set in collect_fn. Set only
    // when true so the flag stays sparse (default-absent).
    if (fi_ptr && (fi_ptr->is_fn_macro || fi_ptr->is_token_macro))
        fn.flag(dk::IS_MACRO_HOOK, true);
    fn.type(dk::RET_TYPE, ret_type);
    fn.block(dk::BODY, body);
    // Phase-1: total dense variable slots assigned in this function body.
    fn.i64_if(dk::LOCAL_COUNT, (int64_t)next_slot_);
    if (!params.empty()) {
        auto a = fn.array(dk::PARAMS);
        for (auto& p : params) a.push_param(p);
    }
    // TYPE_PARAMS: emitted here for free-fn / collected-method callers. The
    // impl-method callers (out_type_params != null) filter method- vs impl-level
    // params AFTER lowering and then emit TYPE_PARAMS / IMPL_TYPE_PARAMS /
    // IMPL_TARGET_PATTERN / IS_PUB / WHERE_TYPE_BOUNDS onto this builder
    // themselves, so we hand them the computed set and skip emitting it here.
    if (out_type_params) {
        *out_type_params = std::move(type_params);
    } else if (!type_params.empty()) {
        auto a = fn.array(dk::TYPE_PARAMS);
        for (auto& tp : type_params) a.push_fn_tparam(tp);
    }
    return fn;
}

// Stage E direct-build: builds the struct/datatype's Writ mirror STRAIGHT into
// the program WritCtr via DeclBuilder (no Draft; struct StructDraft is gone)
// and returns a DeclBuilder the caller finalizes (pkg/doc/flags/type_hash) and
// pushes as a StructView. Local validation still uses the full TypeParam /
// lifetime / field sets (kept as locals); only the fields written into the
// mirror are carried. METHODS array is ALWAYS created (even empty) so later
// in-place appends work. Reproduces the former emit_struct_def_direct mapping.
DeclBuilder SemaChecker::lower_struct_def(TinyMapView node) {
    namespace stk = lir_schema::struct_keys;
    auto sname = std::string(str_of(node.get(la::NAME.code)));
    DeclBuilder sd(*cur_prog_, lir_schema::decl::Code::Struct, /*cap=*/40);
    sd.str_always(stk::NAME, sname);
    sd.str(stk::PKG, cur_package_);
    if (cur_from_binary_) sd.flag(stk::FROM_BINARY_MODULE, true);
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
            sd.array(stk::METHODS);
            sd.bool_always(stk::IS_DATA_PLAIN, true);
            return sd;
        }
    }
    // Carry source-level `pub` onto the mirror (same rationale as lower_fn's
    // dk::IS_PUB): StructView::is_pub() feeds the --emit-abi spec scoping — a
    // NON-pub type is not consumer-nameable, so its methods and its generic
    // instantiations are spec noise. Read the COLLECT-phase truth (sinfo),
    // not the AST node — lower may receive placeholder/re-lowered nodes
    // without the visibility key. Sparse: set only when true.
    if (sinfo->is_pub) sd.flag(stk::IS_PUB, true);
    // Local validation sets (NOT all stored verbatim).
    std::vector<TypeParam>   type_params = sinfo->type_params;
    std::vector<std::string> lifetime_params = sinfo->lifetime_params;
    // Phase 1B-14: propagate custom-DST flag from sema info to LIR.
    if (sinfo->is_dst) sd.flag(stk::IS_DST, true);
    // Writ / RefRepr: propagate `#[self_describing]` so the Ptr→DstRef
    // canonicalisation in mono_subst can keep `*Self` thin for this struct.
    if (sinfo->self_describing) sd.flag(stk::SELF_DESCRIBING, true);
    // Writ: propagate `#[zone_mut]` so ref_repr_of makes `&mut T` a fat
    // {data, zone} reference carrying its allocator.
    if (sinfo->zone_mut) sd.flag(stk::ZONE_MUT, true);
    if (sinfo->zoned2) sd.flag(stk::ZONED2, true);   // writ: auto-relative pointer fields (RelOffset)
    // RefRepr RelOffset: propagate `#[rel_ptr]` so mlir-gen's ref_repr_of can
    // classify this type as a self-relative pointer (8B offset storage).
    if (sinfo->rel_ptr) sd.flag(stk::REL_PTR, true);
    // writ: propagate `#[borrow_carrying]` so the borrow checker escape-tracks
    // values of this type (WAny) like references.
    if (sinfo->borrow_carrying) sd.flag(stk::BORROW_CARRYING, true);
    // `#[non_null]`: single non-null ptr wrapper → Option<T> NullPtr niche in mlir-gen.
    if (sinfo->non_null) sd.flag(stk::NON_NULL, true);
    // §6.1: propagate union flag so mlir-gen's layout path can branch
    // to max-of-fields aligned to max-alignment.
    if (sinfo->is_union) sd.flag(stk::IS_UNION, true);
    // logos-core 1.5: propagate `#[repr(transparent)]` so mlir-gen's
    // `layout_of` Struct case can collapse to the field's layout.
    if (sinfo->repr_transparent) sd.flag(stk::REPR_TRANSPARENT, true);
    // #123: propagate `#[no_auto_drop]` so mlir-gen's recursive drop walk can
    // refuse to destroy a suppressed FIELD — sema's has_droppable_fields only
    // ever answered for the CONTAINER.
    if (sinfo->no_auto_drop) sd.flag(stk::NO_AUTO_DROP, true);
    // B65: outlives bounds from `struct Foo<'a, 'b: 'a>` + validate names.
    auto lifetime_outlives = read_lifetime_outlives(node);
    // B68.3: also pick up where-clause outlives + type-outlives bounds.
    {
        auto where_outlives = read_lifetime_outlives_from(node, la::WHERE.code);
        for (auto& p : where_outlives) lifetime_outlives.push_back(std::move(p));
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
                        for (auto& tp : type_params)
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
        std::unordered_set<std::string> declared(lifetime_params.begin(),
                                                 lifetime_params.end());
        auto known = [&](std::string_view lt) {
            if (lt.empty()) return true;
            if (lt == "'static" || lt == "static") return true;
            return declared.count(std::string(lt)) > 0;
        };
        for (auto& [lng, sht] : lifetime_outlives) {
            if (!known(lng))
                error(std::format("struct '{}': use of undeclared lifetime name '{}' in outlives clause",
                                  sname, lng));
            if (!known(sht))
                error(std::format("struct '{}': use of undeclared lifetime name '{}' in outlives clause",
                                  sname, sht));
        }
        for (auto& tp : type_params) {
            for (auto& lt : tp.lifetime_outlives) {
                if (!known(lt))
                    error(std::format("struct '{}': use of undeclared lifetime name '{}' in `{}: {}` bound",
                                      sname, lt, tp.name, lt));
            }
        }
    }
    push_type_params(type_params);
    // Type-param uniqueness (closes B-gn-01)
    check_unique_names(type_params,
                       [](auto& tp) -> std::string_view { return tp.name; },
                       "type parameter", "struct " + sname);
    // Lifetime-param uniqueness (closes B-gn-02)
    check_unique_names(lifetime_params,
                       [](auto& lt) -> std::string_view { return lt; },
                       "lifetime parameter", "struct " + sname);
    // ── Write the mirror arrays now that validation locals are built. ──
    if (!type_params.empty()) {
        auto ta = sd.array(stk::TYPE_PARAMS);
        for (auto& tp : type_params) ta.push_fn_tparam(tp);
    }
    if (!lifetime_params.empty()) {
        auto la_ = sd.array(stk::LIFETIME_PARAMS);
        for (auto& s : lifetime_params) la_.push_str(s);
    }
    if (!lifetime_outlives.empty()) {
        auto lo = sd.array(stk::LIFETIME_OUTLIVES);
        for (auto& pr : lifetime_outlives) { lo.push_str(pr.first); lo.push_str(pr.second); }
    }
    // Fields (collect into locals first for the uniqueness check, then write).
    std::vector<lir::LField> fields;
    for (auto& f : sinfo->fields)
        fields.push_back({std::string(f.name), f.type, f.is_variadic, f.doc});
    // Field-name uniqueness (closes B-it-03)
    check_unique_names(fields,
                       [](auto& f) -> std::string_view { return f.name; },
                       "field", "struct " + sname);
    if (!fields.empty()) {
        auto fa = sd.array(stk::FIELDS);
        for (auto& f : fields) fa.push_field(f);
    }
    // METHODS array ALWAYS created (even empty) so in-place appends work later.
    auto ma = sd.array(stk::METHODS);
    if (node.has_key(la::ITEMS)) {
        namespace dk = lir_schema::decl_keys;
        auto methods = arr_of(node.get(la::ITEMS.code));
        // Phase A.2: doc-comments in the method stream prime pending_doc_
        // for the next lower_fn invocation.
        pending_doc_.clear();
        // For a GENERIC struct, inline body methods must be lowered with the
        // same generic context an `impl<T> Struct<T>` block establishes
        // (lower_impl): `Self` bound to the generic self-type and the struct's
        // type params recorded as the method's IMPL_TYPE_PARAMS / target
        // pattern. Without it, `-> Self` stays the literal template type
        // `Struct<T...>` and mono never substitutes it at instantiation, so a
        // call like `Pair::<i32,i32>::make(..)` fails with
        // "expected Pair<i32,i32>, got Pair<A,B>". Non-generic structs keep the
        // simple path (lower_fn emits the method's own TYPE_PARAMS directly).
        const bool struct_is_generic = !type_params.empty();
        bool had_self = current_type_params_.count("Self") > 0;
        TypeRef saved_self = had_self ? current_type_params_["Self"] : nullptr;
        std::vector<TypeParam> saved_impl_tps = impl_type_params_;
        TypeRef struct_self = nullptr;
        if (struct_is_generic) {
            std::vector<TypeRef> tv_args;
            tv_args.reserve(type_params.size());
            for (auto& tp : type_params) tv_args.push_back(make_typevar(tp.name));
            struct_self = make_generic_struct(
                sname, std::move(tv_args),
                std::vector<std::string>(lifetime_params), cur_package_);
            current_type_params_["Self"] = struct_self;
            impl_type_params_ = type_params;
        }
        for (uint64_t m = 0; m < methods.size(); ++m) {
            auto method = map_of(methods.get(m));
            if (try_append_doc(pending_doc_, method)) continue;
            int32_t mc = code_of(method);
            if (mc != la::FN && mc != la::STATIC_FN) continue;
            if (!struct_is_generic) {
                auto mfn = lower_fn(method, sname);
                ma.push_ref(mfn.view<lir_view::FunctionView>().self.addr());
                continue;
            }
            // Generic: mirror lower_impl's struct-template method handling.
            std::vector<TypeParam> mtps;
            auto mfn = lower_fn(method, sname, &mtps);
            // Drop struct-level params (mono re-injects them); keep method-level.
            if (!mtps.empty()) {
                std::vector<TypeParam> kept;
                kept.reserve(mtps.size());
                for (auto& tp : mtps) {
                    bool is_struct_level = false;
                    for (auto& stp : type_params)
                        if (stp.name == tp.name) { is_struct_level = true; break; }
                    if (!is_struct_level) kept.push_back(tp);
                }
                mtps = std::move(kept);
            }
            if (!mtps.empty()) {
                auto a = mfn.array(dk::TYPE_PARAMS);
                for (auto& tp : mtps) a.push_fn_tparam(tp);
            }
            // Struct-level params (with bounds) so mono can gate + re-inject them.
            {
                auto a = mfn.array(dk::IMPL_TYPE_PARAMS);
                for (auto& tp : type_params) a.push_fn_tparam(tp);
            }
            mfn.type(dk::IMPL_TARGET_PATTERN, struct_self);
            ma.push_ref(mfn.view<lir_view::FunctionView>().self.addr());
        }
        if (struct_is_generic) {
            if (had_self) current_type_params_["Self"] = saved_self;
            else current_type_params_.erase("Self");
            impl_type_params_ = std::move(saved_impl_tps);
        }
        pending_doc_.clear();
    }
    // is_data_plain defaults true → ALWAYS stored (reader distinguishes false
    // from absent). The caller may overwrite for datatypes.
    sd.bool_always(stk::IS_DATA_PLAIN, true);
    pop_type_params(type_params);
    return sd;
}

// Stage E direct-build: builds the enum's Writ mirror STRAIGHT into the
// program WritCtr via DeclBuilder (no Draft; struct LEnumDef + EnumDraft are
// gone), consumes the pending doc-comment, and returns an EnumView the caller
// pushes. Local validation still uses the full TypeParam set (`tparams`); only
// the fields READ post-construction (name/is_variadic) are stored in the mirror.
lir_view::EnumView SemaChecker::lower_enum_def(TinyMapView node) {
    namespace dk = lir_schema::decl_keys;
    namespace vk = lir_schema::variant_keys;
    namespace tpk = lir_schema::enum_tparam_keys;
    auto ename = std::string(str_of(node.get(la::NAME.code)));
    // Stage E direct-build: NAME always; the rest is added below as it's read.
    DeclBuilder ed(*cur_prog_, lir_schema::decl::Code::Enum, /*cap=*/16);
    ed.str_always(dk::NAME, ename);
    if (!cur_package_.empty()) ed.str(dk::PKG, cur_package_);
    // Local TypeParam set for validation (outlives/uniqueness) — NOT stored.
    std::vector<TypeParam> tparams;
    std::vector<std::string> lifetime_params;
    std::vector<std::pair<std::string, std::string>> lifetime_outlives;
    auto [epkg_led, esi_led] = find_enum_by_name(ename);
    auto eit_led = esi_led ? enums_.find(sema_key(epkg_led, ename)) : enums_.end();
    if (eit_led == enums_.end()) eit_led = enums_.find(ename);
    if (eit_led == enums_.end()) {
        error(std::format("internal: enum '{}' not found in lower_enum_def", ename));
        ed.str(dk::DOC, take_pending_doc());
        return ed.view<lir_view::EnumView>();
    }
    auto& einfo = eit_led->second;
    // Source-level `pub` → mirror from the COLLECT-phase truth (same as
    // lower_struct_def): feeds the --emit-abi private-type spec scoping.
    if (einfo.is_pub) ed.flag(dk::IS_PUB, true);
    tparams = einfo.type_params;
    if (einfo.backing_type)  ed.type(dk::BACKING_TYPE, einfo.backing_type);
    if (einfo.zoned2)          ed.flag(dk::ZONED2, true);   // F3: niche enum's Ref arm self-relative at-rest
    if (einfo.borrow_carrying) ed.flag(dk::BORROW_CARRYING, true);   // WAny: escape-tracked value
    // B65: capture outlives bounds. Enum lifetime_params lives on einfo;
    // outlives bounds re-read from the node.
    lifetime_params = einfo.lifetime_params;
    lifetime_outlives = read_lifetime_outlives(node);
    // B68.3: also pick up where-clause outlives + type-outlives.
    {
        auto where_outlives = read_lifetime_outlives_from(node, la::WHERE.code);
        for (auto& p : where_outlives) lifetime_outlives.push_back(std::move(p));
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
                        for (auto& tp : tparams)
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
        std::unordered_set<std::string> declared(lifetime_params.begin(),
                                                 lifetime_params.end());
        auto known = [&](std::string_view lt) {
            if (lt.empty()) return true;
            if (lt == "'static" || lt == "static") return true;
            return declared.count(std::string(lt)) > 0;
        };
        for (auto& [lng, sht] : lifetime_outlives) {
            if (!known(lng))
                error(std::format("enum '{}': use of undeclared lifetime name '{}' in outlives clause",
                                  ename, lng));
            if (!known(sht))
                error(std::format("enum '{}': use of undeclared lifetime name '{}' in outlives clause",
                                  ename, sht));
        }
        for (auto& tp : tparams) {
            for (auto& lt : tp.lifetime_outlives) {
                if (!known(lt))
                    error(std::format("enum '{}': use of undeclared lifetime name '{}' in `{}: {}` bound",
                                      ename, lt, tp.name, lt));
            }
        }
    }
    // Type-param uniqueness on enum (B-gn-01 family)
    check_unique_names(tparams,
                       [](auto& tp) -> std::string_view { return tp.name; },
                       "type parameter", "enum " + ename);
    // Variant-name uniqueness (closes B-it-04)
    check_unique_names(einfo.variants,
                       [](auto& v) -> std::string_view { return v.name; },
                       "variant", "enum " + ename);
    // VARIANTS array of variant sub-maps (direct-build, mirrors variant_av).
    if (!einfo.variants.empty()) {
        auto va = ed.array(dk::VARIANTS);
        for (auto& v : einfo.variants) {
            auto vb = va.submap(lir_schema::stmt::Count + 3, /*cap=*/8);
            vb.str_always(vk::V_NAME, v.name);
            vb.i64(vk::V_DISC, v.value);
            if (!v.payload_types.empty()) {
                auto pa = vb.array(vk::V_PAYLOAD_TYPES);
                for (auto t : v.payload_types) pa.push_type(t);
            }
            if (v.is_variadic) vb.flag(vk::V_IS_VARIADIC, true);
        }
    }
    // TYPE_PARAMS array of enum-tparam sub-maps. Carry only the fields READ
    // post-construction (name + is_variadic); bounds/const/default are not
    // read for enums (mirrors enum_tparam_av).
    if (!tparams.empty()) {
        auto ta = ed.array(dk::TYPE_PARAMS);
        for (auto& tp : tparams) {
            auto tb = ta.submap(lir_schema::stmt::Count + 4, /*cap=*/4);
            tb.str_always(tpk::TP_NAME, tp.name);
            if (tp.is_variadic) tb.flag(tpk::TP_IS_VARIADIC, true);
        }
    }
    ed.str(dk::DOC, take_pending_doc());
    return ed.view<lir_view::EnumView>();
}


std::pair<DeclBuilder, lir_view::ExprRef>
SemaChecker::lower_const_def(TinyMapView node) {
    namespace dk = lir_schema::decl_keys;
    auto name = std::string(str_of(node.get(la::NAME.code)));
    DeclBuilder lc(*cur_prog_, lir_schema::decl::Code::Const, /*cap=*/12);
    lc.str_always(dk::NAME, name);
    // G156-1: package-scoped consts — carry the owning package so mlir_gen keys
    // its const table by (pkg, name) and same-name cross-package consts coexist.
    if (!cur_package_.empty()) lc.str(dk::PKG, cur_package_);
    // Look up the const's own type under its package-qualified key (matches
    // collect_const's registration); a same-name const in another package has a
    // distinct key and no longer overwrites this one.
    auto cit = module_consts_.find(sema_key(cur_package_, name));
    TypeRef lc_type = (cit != module_consts_.end()) ? cit->second : error_t();
    lc.type(dk::TYPE_REF, lc_type);
    lir_view::ExprRef lc_value;
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
    // resolve_wstatic_value; this lowering only needs the names in scope so
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
        lc_value = lower_expr(map_of(node.get(la::VALUE.code)));
        // B-ca-02: typecheck initializer against declared const type at sema
        // so the diagnostic surfaces here rather than at MLIR-verifier time.
        if (lc_type && lc_value && expr_type(lc_value) &&
            TypeRef(lc_type).kind() != LogosType::Kind::Error &&
            TypeRef(expr_type(lc_value)).kind() != LogosType::Kind::Error &&
            !types_compatible(expr_type(lc_value), lc_type)) {
            // WritStatic special-case: literal evaluates to WStaticLit,
            // which is treated as compatible with WritStatic at higher level.
            bool hs_ok = TypeRef(lc_type).kind() == LogosType::Kind::Struct &&
                         is_writ_static(lc_type) &&
                         TypeRef(expr_type(lc_value)).kind() == LogosType::Kind::WStaticLit;
            if (!hs_ok)
                expect_type(lc_value, lc_type, CoercePos::ConstInit,
                            std::format("const '{}': initializer type mismatch —", name));
        }
    } else {
        lc_value = error_expr();
    }
    for (auto& n : pushed_params) current_type_params_.erase(n);
    return {std::move(lc), lc_value};
}

// Stage E: returns (name, type); the caller adds the doc-comment, emits the
// Writ mirror, and stores a TypeAliasView (struct LTypeAlias is gone).
std::pair<std::string, TypeRef> SemaChecker::lower_type_alias_def(TinyMapView node) {
    auto name = std::string(str_of(node.get(la::NAME.code)));
    auto ait = type_aliases_.find(name);
    // Generic aliases have no concrete LIR type (they're inlined at use sites).
    TypeRef type = (ait != type_aliases_.end() && ait->second.type_params.empty())
                   ? ait->second.type : error_t();
    return {std::move(name), type};
}

// Stage E direct-build: builds the trait decl mirror STRAIGHT into the program's
// WritCtr via DeclBuilder — no C++ LTraitDef Draft. Returns the open builder so
// the caller (apply_annots_to_trait + doc) writes the remaining fields into the
// same mirror in place before pushing the TraitView.
DeclBuilder SemaChecker::lower_trait_def(TinyMapView node) {
    namespace tk  = lir_schema::trait_keys;
    namespace atk = lir_schema::assoc_type_keys;
    namespace tmk = lir_schema::trait_method_keys;
    constexpr uint64_t ASSOC_SCHEMA  = lir_schema::stmt::Count + 13;
    constexpr uint64_t METHOD_SCHEMA = lir_schema::stmt::Count + 14;

    auto tname = std::string(str_of(node.get(la::NAME.code)));
    DeclBuilder b(*cur_prog_, lir_schema::decl::Code::Trait, /*cap=*/16);
    b.str_always(tk::NAME, tname);
    // ADV1-H (dyn-local-trait-shadowing): a user trait whose bare name collides
    // with a prelude/imported same-name trait registers under its package-
    // qualified key (B-mv-02), so the bare `traits_.find(tname)` here would bind
    // this LTraitDef to the WRONG (incumbent) trait's methods / vtable order.
    // Resolve THIS def's own info scope-aware (cur_package::name first), and use
    // its registry key when single-sourcing the vtable layout below. The emitted
    // tk::NAME stays BARE: the impl/vtable ecosystem is bare-keyed and target-
    // disambiguated by design (sema_collect.cpp impls_ key), so mlir-gen's
    // `td_name::target` emit key and `trait_name::type` dispatch key still agree.
    auto tit_scoped = find_trait_iter_scoped(tname);
    std::string vtab_key =
        (tit_scoped != traits_.end()) ? tit_scoped->first : tname;
    auto tit = tit_scoped;
    if (tit != traits_.end()) {
        if (!tit->second.assoc_types.empty()) {
            auto arr = b.array(tk::ASSOC_TYPES);
            for (auto& at : tit->second.assoc_types) {
                auto sub = arr.submap(ASSOC_SCHEMA, /*cap=*/8);
                sub.str_always(atk::AT_NAME, at.name);
                if (!at.bounds.empty()) {
                    auto barr = sub.array(atk::AT_BOUNDS);
                    for (auto& tb : at.bounds) barr.push_tbound(tb);
                }
                sub.str(atk::AT_DOC, at.doc);
            }
        }
        if (!tit->second.methods.empty()) {
            auto arr = b.array(tk::METHODS);
            for (auto& m : tit->second.methods) {
                // Params NOT lowered for trait sigs (may contain Self).
                auto sub = arr.submap(METHOD_SCHEMA, /*cap=*/8);
                sub.str_always(tmk::TM_NAME, m.name);
                sub.type(tmk::TM_RET_TYPE, m.ret_type);
                sub.str(tmk::TM_DOC, m.doc);
            }
        }
    }
    b.str(tk::PKG, cur_package_);
    if (tit != traits_.end()) {
        b.flag(tk::IS_AUTO, tit->second.is_auto);
        {
            auto arr = b.array(tk::SUPERTRAITS);
            for (auto& s : tit->second.supertraits)
                if (s.trait_name != "Copy") arr.push_str(s.trait_name);
        }
        // Single-source the supertrait-closure vtable layout (slot order +
        // ordered upcast targets) for mlir-gen to consume verbatim.
        std::vector<std::pair<std::string, const SemaTraitMethodInfo*>> vtab;
        std::vector<std::string> upcast;
        trait_vtable_layout(vtab_key, vtab, upcast);
        {
            auto arr = b.array(tk::UPCAST_SUPERTRAITS);
            for (auto& u : upcast) arr.push_str(u);
        }
        {
            auto arr = b.array(tk::VTABLE_METHOD_ORDER);
            for (auto& [owner, mptr] : vtab) { arr.push_str(owner); arr.push_str(mptr->name); }
        }
    }
    if (node.has_key(la::TYPE_PARAMS)) {
        // A trait always has an implicit `Self`, and a default type argument may
        // name it — `trait Add<Rhs = Self>` is the canonical operator idiom.
        // collect_trait registers Self before reading its params; the lower pass
        // must do the same, otherwise `Rhs = Self`'s default resolves with Self
        // out of scope → spurious "unknown type 'Self'" (mis-attributed to
        // whatever impl last set ctx_, e.g. a stdlib `impl StableLayout for u8`).
        // Save/restore so the outer scope is not perturbed.
        auto self_it = current_type_params_.find("Self");
        bool had_self = self_it != current_type_params_.end();
        TypeRef prev_self = had_self ? self_it->second : TypeRef(nullptr);
        current_type_params_["Self"] = make_typevar("Self");
        auto tps = read_type_params(node);
        if (had_self) current_type_params_["Self"] = prev_self;
        else          current_type_params_.erase("Self");
        auto arr = b.array(tk::TYPE_PARAMS);
        for (auto& tp : tps) arr.push_str(tp.name);
    }
    return b;
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
    if (getenv("LOGOS_DBG_BSPEC")) {
        size_t nb = 0; for (auto& tp : impl_tps) nb += tp.bounds.size();
        fprintf(stderr, "[implE] trait=%s tps=%zu bounds=%zu\n",
                trait_name.c_str(), impl_tps.size(), nb);
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
    // Stage E direct-build: the impl decl mirror is built STRAIGHT into prog's
    // WritCtr via DeclBuilder — no C++ LImplBlock Draft. Only the live
    // (read-post-store) fields are written; dead fields (is_unsafe/methods/doc/
    // trait_lifetime_args) are dropped. Intra-function reads of is_blanket /
    // bound_trait / target_typeref go through plain locals (the mirror is
    // write-only here).
    namespace ik  = lir_schema::impl_keys;
    namespace aek = lir_schema::assoc_entry_keys;
    namespace eek = lir_schema::extra_eq_keys;
    constexpr uint64_t ASSOC_ENTRY_SCHEMA = lir_schema::stmt::Count + 15;
    constexpr uint64_t EXTRA_EQ_SCHEMA    = lir_schema::stmt::Count + 16;

    DeclBuilder ib(prog, lir_schema::decl::Code::Impl, /*cap=*/16);
    bool    impl_is_blanket = false;
    std::string impl_bound_trait;
    TypeRef impl_target_typeref = target_resolved;

    ib.str(ik::TRAIT_NAME, trait_name);
    // Trait IDENTITY beside the spelling (impl_keys::CANONICAL_TRAIT). traits_
    // is fully collected by the time decls lower, so canonical_trait_name
    // resolves here without threading SemaImplInfo::canonical_trait through.
    // Written only when it DIFFERS from the bare spelling — an impl of the
    // trait that owns the bare slot needs no extra bytes, and mono's
    // ImplView::canonical_trait() falls back to trait_name().
    if (!trait_name.empty()) {
        std::string canon = canonical_trait_name(trait_name);
        if (canon != trait_name) ib.str(ik::CANONICAL_TRAIT, canon);
        // ⚠ AND THE ALWAYS-QUALIFIED IDENTITY BESIDE IT. `canon` above is the
        // traits_ REGISTRY key and is BARE for whichever homonym owns the bare
        // slot, so mono keying its fact table by it put two traits in one slot.
        std::string ident = impl_key_trait(canon);
        if (ident != trait_name) ib.str(ik::IDENTITY_TRAIT, ident);
    }
    ib.str(ik::TARGET_TYPE, target);
    // CP-cm-16 follow-up: full impl-target pattern with TypeVars unsubstituted.
    // Set for generic-target impls (`impl<T,E> ... for Foo<Vec<T>, E>`) so
    // mono can pattern-unify against the concrete receiver. Null for the
    // concrete + non-generic + primitive + special-target cases (only
    // GENERIC_INST + non-empty impl_tps populates target_resolved on
    // this path; mono falls back to positional binding when null).
    ib.type(ik::TARGET_TYPEREF, target_resolved);
    if (!impl_tps.empty()) {
        auto a = ib.array(ik::IMPL_TYPE_PARAMS);
        for (auto& tp : impl_tps) a.push_fn_tparam(tp);
    }
    // B62: copy trait-arg region info captured by collect_impl, so mono's
    // method_bound_ok can detect HRTB satisfaction mismatch.
    if (!trait_name.empty()) {
        auto it = impls_.find(trait_name + "::" + target);
        if (it != impls_.end()) {
            if (!it->second.trait_type_args.empty()) {
                auto a = ib.array(ik::TRAIT_TYPE_ARGS);
                for (auto t : it->second.trait_type_args) a.push_type(t);
            }
            // trait_lifetime_args: DEAD (never read post-store) — not mirrored.
            if (!it->second.impl_lifetime_params.empty()) {
                auto a = ib.array(ik::IMPL_LIFETIME_PARAMS);
                for (auto& lp : it->second.impl_lifetime_params) a.push_str(lp);
            }
            if (!it->second.impl_lifetime_outlives.empty()) {  // B65
                auto a = ib.array(ik::LIFETIME_OUTLIVES);
                for (auto& [lng, sht] : it->second.impl_lifetime_outlives) {
                    a.push_str(lng); a.push_str(sht);
                }
            }
            // Validate impl-level outlives names against declared impl_lifetime_params.
            std::unordered_set<std::string> declared(it->second.impl_lifetime_params.begin(),
                                                     it->second.impl_lifetime_params.end());
            auto known = [&](std::string_view lt) {
                if (lt.empty()) return true;
                if (lt == "'static" || lt == "static") return true;
                return declared.count(std::string(lt)) > 0;
            };
            for (auto& [lng, sht] : it->second.impl_lifetime_outlives) {
                if (!known(lng))
                    error(std::format("impl {} for {}: use of undeclared lifetime name '{}' in outlives clause",
                                      trait_name, target, lng));
                if (!known(sht))
                    error(std::format("impl {} for {}: use of undeclared lifetime name '{}' in outlives clause",
                                      trait_name, target, sht));
            }
        }
    }
    // Propagate genos type_code: `impl Varchar for WritString` on a
    // trait that carries #[type_code=N] sets the target struct's type_code
    // (if the struct hasn't got one already).  This is how eide inherit
    // their identity from the logical datatype family.
    if (!trait_name.empty() && !target.empty()) {
        for (const auto& td : prog.traits) {
            if (td.name() != trait_name || td.type_code() == 0) continue;
            uint64_t td_type_code = td.type_code();
            bool applied = false;
            for (auto& sd : prog.structs) {
                if (sd.name() != target) continue;
                // Trait-declared type_code wins over the hash-derived default
                // auto-assigned at eidos lowering time.  An explicit
                // `#[type_code]` on the eidos itself would normally also
                // win, but we don't allow both at the moment.
                lir_mirror_struct_set_type_code(prog, sd, td_type_code);
                auto fqn = cur_package_.empty() ? std::string(sd.name())
                                                 : cur_package_ + "::" + std::string(sd.name());
                explicit_type_codes_[fqn] = td_type_code;
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
                namespace iak = lir_schema::inst_annot_keys;
                DeclBuilder ia(prog, lir_schema::decl::Code::InstAnnot, /*cap=*/8);
                ia.str(iak::MANGLED_NAME, target);
                ia.i64(iak::TYPE_CODE, (int64_t)td_type_code);
                ia.type(iak::STRUCT_TYPE, target_resolved);  // for mono struct demand
                // Derive canonical "pkg::BaseName<Args>" from the target type
                // (use target_resolved captured earlier — always set when the
                // target is a concrete generic instantiation, i.e. this path).
                std::string pkg;
                if (target_resolved) {
                    // Use get_*_si which handles pkg_name correctly
                    if (auto* dsi_tr = get_datatype_si(target_resolved)) pkg = dsi_tr->package;
                    else if (auto* ssi_tr = get_struct_si(target_resolved)) pkg = ssi_tr->package;
                    if (pkg.empty()) pkg = cur_package_;
                    std::string canonical = pkg + "::" + type_str(target_resolved);
                    ia.str(iak::CANONICAL_NAME, canonical);
                    explicit_type_codes_[canonical] = td_type_code;
                }
                // Also register the mangled-form key ("pkg::Array$G1$AnyVal")
                // so the dispatch-entry emission code (which looks up by mangled
                // target) picks up the propagated type_code.
                {
                    std::string p = pkg.empty() ? std::string(cur_package_) : pkg;
                    std::string mangled_fqn = p.empty() ? target : p + "::" + target;
                    explicit_type_codes_[mangled_fqn] = td_type_code;
                }
                prog.inst_annotations.push_back(ia.view<lir_view::InstAnnotView>());
            }
            break;
        }
    }
    // Blanket impl detection: target IS one of this impl's own type parameters.
    if (!trait_name.empty()) {
        for (auto& tp : impl_tps) {
            if (tp.name == target) {
                impl_is_blanket = true;
                ib.flag(ik::IS_BLANKET, true);
                if (!tp.bounds.empty()) {
                    impl_bound_trait = tp.bounds[0].trait_name;
                    ib.str(ik::BOUND_TRAIT, tp.bounds[0].trait_name);
                    // The blanket's own bound, by identity. Without this the
                    // bound reaches mono as raw text and mono admits whichever
                    // homonym's concretes happen to be filed under it — which
                    // is how `impl<T: Hash> Marker for T` instantiated
                    // `i32__tag` for a Marker nobody called on an i32.
                    if (!tp.bounds[0].identity_trait.empty() &&
                        tp.bounds[0].identity_trait != tp.bounds[0].trait_name)
                        ib.str(ik::IDENTITY_BOUND_TRAIT, tp.bounds[0].identity_trait);
                    if (!tp.bounds[0].assoc_eqs.empty()) {
                        auto a = ib.array(ik::PRIMARY_ASSOC_EQS);
                        for (auto& [n, t] : tp.bounds[0].assoc_eqs) {
                            auto e = a.submap(ASSOC_ENTRY_SCHEMA, 4);
                            e.str_always(aek::AE_NAME, n);
                            e.type(aek::AE_TYPE, t);
                        }
                    }
                    if (tp.bounds.size() > 1) {
                        auto eb = ib.array(ik::EXTRA_BOUNDS);
                        auto ieb = ib.array(ik::IDENTITY_EXTRA_BOUNDS);
                        auto ee = ib.array(ik::EXTRA_ASSOC_EQS);
                        for (size_t bi = 1; bi < tp.bounds.size(); ++bi) {
                            eb.push_str(tp.bounds[bi].trait_name);
                            // Positional twin of EXTRA_BOUNDS — same length, so
                            // a reader may index the two together. Falls back to
                            // the spelling when the identity was not captured.
                            ieb.push_str(tp.bounds[bi].identity_trait.empty()
                                             ? tp.bounds[bi].trait_name
                                             : tp.bounds[bi].identity_trait);
                            auto m = ee.submap(EXTRA_EQ_SCHEMA, 4);
                            m.str_always(eek::EE_TRAIT, tp.bounds[bi].trait_name);
                            if (!tp.bounds[bi].assoc_eqs.empty()) {
                                auto a = m.array(eek::EE_EQS);
                                for (auto& [n, t] : tp.bounds[bi].assoc_eqs) {
                                    auto e = a.submap(ASSOC_ENTRY_SCHEMA, 4);
                                    e.str_always(aek::AE_NAME, n);
                                    e.type(aek::AE_TYPE, t);
                                }
                            }
                        }
                    }
                }
                break;
            }
        }
    }
    // is_unsafe: DEAD (never read post-store) — consumed but not mirrored.
    // Resolve trait type args and push into scope
    std::vector<TypeRef> impl_trait_args;
    if (!trait_name.empty() && node.has_key(la::TYPE_PARAMS)) {
        AnyVal tpav = node.get(la::TYPE_PARAMS.code);
        if (!tpav.is_null()) {
            auto tplist = map_of(tpav);
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                // #100: SCOPED (see the ground at the default-body loop below/above).
                auto tit = find_trait_iter_scoped(trait_name);
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
    // the correct `Trait$G..$Args` suffix. ib.array() re-creates a fresh empty
    // array under the key, overwriting the collect-seeded one above.
    if (!impl_trait_args.empty()) {
        auto a = ib.array(ik::TRAIT_TYPE_ARGS);
        for (auto t : impl_trait_args) a.push_type(t);
    }
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
                if (sd.name() != target) continue;
                lir_mirror_struct_set_type_code(prog, sd, eit->second);
                auto fqn = cur_package_.empty() ? std::string(sd.name())
                                                 : cur_package_ + "::" + std::string(sd.name());
                explicit_type_codes_[fqn] = eit->second;
                break;
            }
        }
    }
    // Lower impl methods as free functions (Target__method).
    // For `impl<T> GenericStruct<T>` blocks, add methods to the struct template instead of
    // prog.functions so mono's instantiate_one_struct can clone them with T substituted.
    // For `impl<V> PartialSpec<Concrete, V>` attach to the matching partial spec
    // (so mono picks up methods when instantiating the spec, not the base template).
    lir_view::StructView* target_struct_tmpl = nullptr;
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
                    const TypePoolImpl* sd_pool = prog.type_pool.impl();
                    for (auto& ss : prog.struct_specializations) {
                        if (ss.name() != base_name) continue;
                        auto ss_pats = ss.spec_patterns(sd_pool);
                        if (ss_pats.size() != TypeRef(target_type).type_args().size()) continue;
                        bool match = true;
                        for (size_t i = 0; i < ss_pats.size(); ++i) {
                            auto a = TypeRef(target_type).type_args()[i];
                            auto p = ss_pats[i];
                            if (!a || !p) { match = false; break; }
                            if (TypeRef(a).kind() == LogosType::Kind::TypeVar &&
                                TypeRef(p).kind() == LogosType::Kind::TypeVar) {
                                // Both TypeVar: for a BOUND-DISCRIMINATED spec
                                // the impl belongs to it only when the impl
                                // var's bound-set EQUALS the pattern var's
                                // (impl<T: Copy+Fst> S<T> → the bounded spec;
                                // impl<T: ?Sized> S<T> → the base template).
                                std::vector<std::string> pat_bounds;
                                for (auto stp : ss.type_params()) {
                                    if (stp.name() != TypeRef(p).type_var_name())
                                        continue;
                                    stp.each_bound([&](lir_view::FnTraitBoundView b) {
                                        pat_bounds.push_back(std::string(b.trait_name()));
                                    });
                                    break;
                                }
                                std::vector<std::string> impl_bounds;
                                for (auto& itp : impl_tps) {
                                    if (itp.name != TypeRef(a).type_var_name())
                                        continue;
                                    for (auto& b : itp.bounds)
                                        impl_bounds.push_back(b.trait_name);
                                    break;
                                }
                                std::sort(pat_bounds.begin(), pat_bounds.end());
                                std::sort(impl_bounds.begin(), impl_bounds.end());
                                if (pat_bounds != impl_bounds) { match = false; break; }
                                continue;
                            }
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
                if (sd.name() == target && sd.pkg() == cur_package_) {
                    target_struct_tmpl = &sd; break;
                }
            if (!target_struct_tmpl) {
                for (auto& sd : prog.structs)
                    if (sd.name() == target) { target_struct_tmpl = &sd; break; }
            }
        }
    }
    StrSet overridden;
    // Blanket impls lower methods under a synthetic target name so they don't
    // collide with `T::method` for any other generic `T` in the program.
    std::string lower_target = impl_is_blanket
        ? ("$blanket$" + trait_name + "$" + impl_bound_trait + "$" + target)
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
                namespace dk = lir_schema::decl_keys;
                // Direct-build: lower_fn returns the open builder; type_params are
                // handed back so we can filter method- vs impl-level params, then
                // emit the deferred TYPE_PARAMS / IMPL_* / IS_PUB onto it here.
                std::vector<TypeParam> type_params;
                auto fn = lower_fn(m, lower_target, &type_params);
                // Trait-impl methods inherit visibility from the trait itself:
                // if the trait is reachable, its methods are callable (Rust
                // semantics).  The grammar does not allow `pub` on trait
                // methods, so force is_pub=true when lowering under a trait
                // impl block.  Inherent-impl methods (no trait_name) keep the
                // explicit `pub fn` / private split.
                if (!trait_name.empty()) fn.flag(dk::IS_PUB, true);
                overridden.insert(std::string(fn.view<lir_view::FunctionView>().name()));
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
                    if (!type_params.empty()) {
                        std::vector<TypeParam> kept;
                        kept.reserve(type_params.size());
                        for (auto& tp : type_params) {
                            bool is_impl_level = false;
                            for (auto& itp : impl_tps)
                                if (itp.name == tp.name) { is_impl_level = true; break; }
                            if (!is_impl_level) kept.push_back(tp);
                        }
                        type_params = std::move(kept);
                    }
                    if (!type_params.empty()) {
                        auto a = fn.array(dk::TYPE_PARAMS);
                        for (auto& tp : type_params) a.push_fn_tparam(tp);
                    }
                    // Preserve impl-level type params with their bounds so mono
                    // can gate instantiation on bound satisfaction.
                    if (!impl_tps.empty()) {
                        auto a = fn.array(dk::IMPL_TYPE_PARAMS);
                        for (auto& tp : impl_tps) a.push_fn_tparam(tp);
                    }
                    // Structured impl self-type (`impl<T> Pin<&T>`): mono needs
                    // the pattern to map impl-level args ([T]) to the struct's
                    // concrete args ([&T]) — positional copy mis-names specs.
                    fn.type(dk::IMPL_TARGET_PATTERN, impl_target_typeref);
                    lir_mirror_struct_append_method(prog, *target_struct_tmpl, fn.view<lir_view::FunctionView>());
                } else {
                    // CP-cm-16 follow-up: enum-impl path (impl<T,E> Trait for
                    // Result<Vec<T>, E>). Methods go to prog.functions with
                    // impl-level T,E flattened into fn.type_params (per the
                    // existing comment at mono_clone.cpp:4319). Carry the
                    // impl-target pattern so mono's instantiate_enum_templates
                    // can unify pattern↔receiver instead of positional binding.
                    if (!type_params.empty()) {
                        auto a = fn.array(dk::TYPE_PARAMS);
                        for (auto& tp : type_params) a.push_fn_tparam(tp);
                    }
                    fn.type(dk::IMPL_TARGET_PATTERN, impl_target_typeref);
                    prog.functions.push_back(fn.view<lir_view::FunctionView>());
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
                    namespace dk = lir_schema::decl_keys;
                    DeclBuilder acc(prog, lir_schema::decl::Code::Func, /*cap=*/40);
                    acc.str_always(dk::NAME, lower_target + "__kassoc_" + cname);
                    acc.str(dk::METHOD_BASE, "kassoc_" + cname);
                    acc.str(dk::PKG, cur_package_);
                    acc.type(dk::RET_TYPE, ctype ? ctype
                                            : (val ? expr_type(val) : void_t()));
                    acc.flag(dk::IS_PUB, true);
                    acc.str(dk::SOURCE_FILE, file_);
                    acc.str(dk::UNIT_KEY, cur_unit_key_);   // UnitGraph §1.2
                    {
                        std::vector<lir_view::StmtRef> acc_body;
                        acc_body.push_back(builder().stmt_return(val, 0));
                        acc.block(dk::BODY, lir_mirror_block(*cur_prog_, acc_body));
                    }
                    prog.functions.push_back(acc.view<lir_view::FunctionView>());
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
        // #100: SCOPED, not bare. `traits_.find(trait_name)` answers for whichever
        // homonym owns the BARE slot, so a user `trait ExactSizeIterator` read the
        // STDLIB trait here while collect_impl (find_trait_iter_scoped) read the
        // user's. The two disagreeing is the whole defect: collect registered the
        // user method list, lower_impl_block synthesised the STDLIB default bodies
        // into the user impl and emitted them (MEASURED: `T ZqH__is_empty`, an
        // unqualified global, from a 5-line user file that never wrote is_empty).
        auto tit = find_trait_iter_scoped(trait_name);
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
                    // §8.5: per-method where-clause gate. A method like
                    // `fn max() where Item: Ord` is only synthesised for
                    // an impl whose concrete trait-arg satisfies the
                    // bound (Rust conditional-default semantics). For
                    // each (param_name, trait_name) bound: locate the
                    // trait-param's index, map to the impl's concrete
                    // arg, check `concrete: trait` via the recursive
                    // impls_ probe. A failing bound silently skips
                    // default synthesis — the method is unavailable
                    // for this impl. Blanket impls (Self = TypeVar) and
                    // generic-impl args (TypeVar args) defer: the
                    // concrete is unknown at sema, so synthesise the
                    // default template and let mono error if needed.
                    bool gate_skip = false;
                    if (!impl_is_blanket && !m.where_param_bounds.empty()) {
                        // A trait-arg that still mentions a TypeVar anywhere
                        // (`&T`, `Option<T>`, `(T,U)`, …) is NOT decidable at
                        // sema — the concrete is only known after mono
                        // substitutes the impl's params. Defer such gates to
                        // mono (which re-checks via `method_bound_ok`). The
                        // old shallow `kind()==TypeVar` test only deferred a
                        // bare top-level `T`, so `impl<T> Iterator<&T> for
                        // VecIter<T>` wrongly computed `&T: Ord` (always
                        // false) and SKIPPED synthesising `max`/`min` for
                        // every by-ref iterator. Recurse like mono's
                        // `contains_typevar`.
                        std::function<bool(TypeRef)> mentions_tv = [&](TypeRef t) -> bool {
                            if (!t) return false;
                            if (t.kind() == LogosType::Kind::TypeVar) return true;
                            if (t.kind() == LogosType::Kind::Error)   return true;
                            if (t.pointee() && mentions_tv(t.pointee())) return true;
                            if (t.elem()    && mentions_tv(t.elem()))    return true;
                            for (auto a : t.type_args())    if (mentions_tv(a)) return true;
                            for (auto e : t.tuple_elems())  if (mentions_tv(e)) return true;
                            for (auto p : t.closure_params())if (mentions_tv(p)) return true;
                            if (t.closure_ret() && mentions_tv(t.closure_ret())) return true;
                            return false;
                        };
                        for (auto& wb : m.where_param_bounds) {
                            // Find the trait-param index by name.
                            size_t pidx = SIZE_MAX;
                            for (size_t pi = 0; pi < tit->second.type_params.size(); ++pi)
                                if (tit->second.type_params[pi].name == wb.param_name) {
                                    pidx = pi; break;
                                }
                            if (pidx == SIZE_MAX) continue;
                            if (pidx >= impl_trait_args.size()) continue;
                            TypeRef concrete = impl_trait_args[pidx];
                            if (!concrete) continue;
                            TypeRef cv{concrete};
                            // Not fully concrete (any nested TypeVar/Error):
                            // defer to mono.
                            if (mentions_tv(cv)) continue;
                            std::string cstr = type_str(concrete);
                            logos::compiler::StrSet seen;
                            if (!sema_has_impl_recursive(wb.trait_name, cstr, /*alt=*/"", seen)) {
                                gate_skip = true;
                                break;
                            }
                        }
                    }
                    if (gate_skip) continue;
                    // Push Self → target type; for generic impls include type params as TypeVars.
                    TypeRef self_type = nullptr;
                    if (impl_is_blanket) {
                        self_type = make_typevar(target);
                    } else {
                        auto [spkg2, ssi2] = find_struct_by_name(target);
                        auto [dpkg2, dsi2] = find_datatype_by_name(target);
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
                        if (ssi2) {
                            if (impl_target_typeref && _shaped_target(impl_target_typeref)) {
                                // Shaped generic impl (`impl<I,T> … for
                                // CopiedIter<I, &T>`): Self must be the impl's
                                // structured TARGET PATTERN, not base-name +
                                // positional impl params (CopiedIter<I, T>) —
                                // the positional shape mis-unified self at
                                // every call (`.max()` inferred T=&i32).
                                self_type = impl_target_typeref;
                            } else if (!impl_tps.empty()) {
                                std::vector<TypeRef> tv_args;
                                for (auto& tp : impl_tps)
                                    tv_args.push_back(make_typevar(tp.name));
                                self_type = make_generic_struct(target, std::move(tv_args), {}, spkg2);
                            } else {
                                self_type = make_struct_type(target, spkg2);
                            }
                        } else if (dsi2) {
                            if (impl_target_typeref && _shaped_target(impl_target_typeref)) {
                                self_type = impl_target_typeref;  // same shape rule
                            } else if (!impl_tps.empty()) {
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
                    namespace dk = lir_schema::decl_keys;
                    std::vector<TypeParam> type_params;
                    auto fn = lower_fn(map_of(m.default_ast), lower_target, &type_params);
                    holder_ = saved_holder;
                    fn.flag(dk::IS_PUB, true);  // default trait method inherits trait visibility
                    // §8.5: carry every per-method where-bound as a
                    // type-EXPRESSION bound, expressed in the impl's
                    // generic terms (the trait-arg `impl_trait_args[pidx]`,
                    // e.g. `&T` for `impl<T> Iterator<&T> for VecIter<T>`).
                    // Mono substitutes the subject with the clone's concrete
                    // args and re-gates via `method_bound_ok`. This is the
                    // real gate for compound-Item iterators: sema can only
                    // decide bare-concrete Items, so non-Ord ones
                    // (EnumPair<T>, [T;0], &[T]) are deferred here and
                    // rejected at mono — without it, deferral would
                    // synthesise `max`/`min` for every iterator and the
                    // `iter_max` body would fail to typecheck.
                    std::vector<std::pair<TypeRef, std::string>> where_type_bounds;
                    if (!impl_is_blanket) {
                        for (auto& wb : m.where_param_bounds) {
                            size_t pidx = SIZE_MAX;
                            for (size_t pi = 0; pi < tit->second.type_params.size(); ++pi)
                                if (tit->second.type_params[pi].name == wb.param_name) {
                                    pidx = pi; break;
                                }
                            if (pidx == SIZE_MAX || pidx >= impl_trait_args.size()) continue;
                            TypeRef subj = impl_trait_args[pidx];
                            if (!subj) continue;
                            where_type_bounds.emplace_back(subj, wb.trait_name);
                        }
                    }
                    if (!where_type_bounds.empty()) {
                        auto a = fn.array(dk::WHERE_TYPE_BOUNDS);
                        for (auto& wb : where_type_bounds) a.push_wherebound(wb);
                    }
                    // Generic impl: the default method must travel as a struct-
                    // template method so mono clones it per concrete struct
                    // instantiation. Otherwise the bare-name template ends up as
                    // a free fn that mono only clones on explicit callsites —
                    // and dyn-trait dispatch is NOT a callsite.
                    if (target_struct_tmpl) {
                        if (!type_params.empty()) {
                            std::vector<TypeParam> kept;
                            kept.reserve(type_params.size());
                            for (auto& tp : type_params) {
                                bool is_impl_level = false;
                                for (auto& itp : impl_tps)
                                    if (itp.name == tp.name) { is_impl_level = true; break; }
                                if (!is_impl_level) kept.push_back(tp);
                            }
                            type_params = std::move(kept);
                        }
                        if (!type_params.empty()) {
                            auto a = fn.array(dk::TYPE_PARAMS);
                            for (auto& tp : type_params) a.push_fn_tparam(tp);
                        }
                        std::vector<TypeParam> impl_type_params = impl_tps;
                        // §8.5: propagate where-clause TRAIT-param bounds
                        // (e.g. `Item: Ord`) onto the now-final
                        // impl_type_params so mono's `method_bound_ok`
                        // rejects a clone whose substituted concrete arg
                        // doesn't implement the required trait. Must run
                        // AFTER `impl_type_params = impl_tps` (which
                        // overwrites bounds added earlier).
                        for (auto& wb : m.where_param_bounds) {
                            size_t pidx = SIZE_MAX;
                            for (size_t pi = 0; pi < tit->second.type_params.size(); ++pi)
                                if (tit->second.type_params[pi].name == wb.param_name) {
                                    pidx = pi; break;
                                }
                            if (pidx == SIZE_MAX || pidx >= impl_trait_args.size()) continue;
                            TypeRef arg = impl_trait_args[pidx];
                            if (!arg) continue;
                            TypeRef av{arg};
                            if (av.kind() != LogosType::Kind::TypeVar) continue;
                            std::string tvname(av.type_var_name());
                            for (auto& tp : impl_type_params) {
                                if (tp.name != tvname) continue;
                                bool dup = false;
                                for (auto& b : tp.bounds)
                                    if (b.trait_name == wb.trait_name) { dup = true; break; }
                                if (!dup) {
                                    TraitBound tb;
                                    tb.trait_name = wb.trait_name;
                                    tp.bounds.push_back(std::move(tb));
                                }
                                break;
                            }
                        }
                        if (!impl_type_params.empty()) {
                            auto a = fn.array(dk::IMPL_TYPE_PARAMS);
                            for (auto& tp : impl_type_params) a.push_fn_tparam(tp);
                        }
                        fn.type(dk::IMPL_TARGET_PATTERN, impl_target_typeref);
                        lir_mirror_struct_append_method(prog, *target_struct_tmpl, fn.view<lir_view::FunctionView>());
                    } else {
                        // CP-cm-16 follow-up: parallel propagation for
                        // trait-default methods on enum-impl path.
                        if (!type_params.empty()) {
                            auto a = fn.array(dk::TYPE_PARAMS);
                            for (auto& tp : type_params) a.push_fn_tparam(tp);
                        }
                        fn.type(dk::IMPL_TARGET_PATTERN, impl_target_typeref);
                        prog.functions.push_back(fn.view<lir_view::FunctionView>());
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
        // #100: SCOPED (see the ground at the default-body loop below/above).
        auto tit = find_trait_iter_scoped(trait_name);
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
        std::string stored_target = impl_is_blanket
            ? ("$blanket$" + trait_name + "$" + impl_bound_trait + "$" + target)
            : target;
        // G156-1: assoc-type impls are registered under a trait-arg-suffixed key
        // (e.g. "Producer$G1$i64::Gen::Item"); for two `Trait<T>` impls of one
        // type the bare plain key is erased, so match THIS impl's suffixed
        // prefix. Empty suffix (non-generic trait) → bare prefix, unchanged.
        auto prefix = trait_name + trait_targ_suffix(impl_trait_args)
                    + "::" + stored_target + "::";
        DeclArrayBuilder at_arr = ib.array(ik::ASSOC_TYPES);
        for (auto& [key, entry] : assoc_type_impls_) {
            if (key.rfind(prefix, 0) == 0) {
                auto assoc_name = key.substr(prefix.size());
                auto e = at_arr.submap(ASSOC_ENTRY_SCHEMA, 4);
                e.str_always(aek::AE_NAME, assoc_name);
                e.type(aek::AE_TYPE, entry.type);
            }
        }
        // const-length-overhaul: emit this impl's ASSOC-CONST VALUES so mono can
        // fold a compile-time `C::CONST` projection once C binds. assoc_const_impls_
        // is keyed "<trait>::<target>::<name>" (no trait-arg suffix). Each value
        // is ctfe'd to an i64 here (sema is the only place that can).
        namespace ack = lir_schema::assoc_const_keys;
        constexpr uint64_t ASSOC_CONST_SCHEMA = lir_schema::stmt::Count + 17;
        std::string cprefix = trait_name + "::" + target + "::";
        DeclArrayBuilder ac_arr = ib.array(ik::ASSOC_CONSTS);
        for (auto& [key, entry] : assoc_const_impls_) {
            if (key.rfind(cprefix, 0) != 0) continue;
            if (entry.value_ast.is_null()) continue;
            auto v = ctfe_eval_const(map_of(entry.value_ast), holder_);
            if (!v) continue;   // non-const-foldable value — skip (used via accessor)
            auto cname = key.substr(cprefix.size());
            auto e = ac_arr.submap(ASSOC_CONST_SCHEMA, 4);
            e.str_always(ack::AC_NAME, cname);
            e.i64(ack::AC_VALUE, v.value().i);
        }
    }
    // doc: DEAD (never read post-store) — impl_doc consumed above, not mirrored.
    (void)impl_doc;
    prog.impls.push_back(ib.view<lir_view::ImplView>());

    // ── Tag-dispatch: emit LDispatchEntry records ─────────────────────────
    // Conditions: trait has #[tag_dispatch(TS)], target is a concrete (non-generic)
    // datatype with a known type_code, impl is not a generic impl block.
    if (!trait_name.empty() && impl_tps.empty()) {
        std::string tag_system;
        for (auto& td : prog.traits) {
            if (td.name() == trait_name) { tag_system = std::string(td.tag_dispatch_system()); break; }
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
                if (sd.is_zoned() && sd.name() == target && sd.type_code() != 0) {
                    tcode = sd.type_code(); break;
                }
            }
            if (tcode == 0) {
                // Also check explicit_type_codes_ for types annotated but not yet in prog.structs.
                // Keys are fully-qualified ("pkg::Name"). Try the current package first
                // (for same-file impls), then the target type's own package (for impls
                // on foreign types, e.g. `impl WritEqual for WritString` in
                // package writ.equal — WritString was annotated in writ.string).
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
                // #100: SCOPED (see the ground at the default-body loop below/above).
                auto tit = find_trait_iter_scoped(trait_name);
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

                        namespace dpk = lir_schema::dispatch_keys;
                        DeclBuilder de(prog, lir_schema::decl::Code::DispatchEntry, /*cap=*/8);
                        de.str(dpk::TAG_SYSTEM, tag_system);
                        de.str(dpk::TRAIT_NAME, trait_name);
                        de.str(dpk::METHOD_NAME, m.name);
                        de.str(dpk::FN_SYMBOL, actual_sym);
                        de.str(dpk::IMPL_TYPE_NAME, target);
                        de.i64(dpk::TYPE_CODE, (int64_t)tcode);
                        prog.dispatch_entries.push_back(de.view<lir_view::DispatchEntryView>());
                    }
                }
            }
        }
    }
}
} // namespace logos::compiler
