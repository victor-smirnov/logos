// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include "sema_impl.hpp"

#include <cstdio>
#include <format>
#include <functional>

namespace logos::compiler {

namespace la = ast;
using hermes::TinyMapView;
using hermes::ArrayView;
using hermes::StringView;
using hermes::AnyVal;
using hermes::MemHolder;

// Declaration lowering methods

// Recursively evaluate a static Hermes literal AST node into a HermesVal tree.
// Rejects capture nodes ($ident, ${expr}) — meta @{} is compile-time only.
// Strip surrounding double quotes and basic escape sequences from a raw token string.
static std::string unquote_str(std::string_view sv) {
    std::string s(sv);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            ++i;
            switch (s[i]) {
                case 'n':  out += '\n'; break;
                case 't':  out += '\t'; break;
                case 'r':  out += '\r'; break;
                case '\\': out += '\\'; break;
                case '"':  out += '"';  break;
                default:   out += s[i]; break;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

lir::HermesValPtr SemaChecker::eval_static_hermes_lit(TinyMapView node) {
    using namespace lir;
    int32_t code = code_of(node);

    if (code == la::HERMES_MAP.code) {
        HVMap map;
        if (node.has_key(la::ITEMS) && !node.get(la::ITEMS.code).is_null()) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto entry = map_of(items.get(i));
                if (code_of(entry) != la::HERMES_ENTRY.code) continue;
                std::string key = unquote_str(str_of(entry.get(la::KEY.code)));
                auto val_node   = map_of(entry.get(la::VALUE.code));
                auto val        = eval_static_hermes_lit(val_node);
                if (!val) return nullptr;  // propagate error
                lir::HVMapEntry me; me.key = std::move(key); me.val = std::move(val);
                map.entries.push_back(std::move(me));
            }
        }
        return alloc_hv_emit(std::move(map));
    }
    if (code == la::HERMES_ARRAY.code) {
        HVArray arr;
        if (node.has_key(la::ITEMS) && !node.get(la::ITEMS.code).is_null()) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto elem = eval_static_hermes_lit(map_of(items.get(i)));
                if (!elem) return nullptr;
                arr.elements.push_back(std::move(elem));
            }
        }
        return alloc_hv_emit(std::move(arr));
    }
    if (code == la::HERMES_STR.code) {
        return alloc_hv_emit(HVStr{unquote_str(str_of(node.get(la::VALUE.code)))});
    }
    if (code == la::HERMES_INT.code) {
        std::string s(str_of(node.get(la::VALUE.code)));
        int64_t v = s.empty() ? 0 : (int64_t)std::stoull(s, nullptr, 0);
        return alloc_hv_emit(HVInt{v});
    }
    if (code == la::HERMES_NEG_INT.code) {
        std::string s(str_of(node.get(la::VALUE.code)));
        int64_t v = s.empty() ? 0 : -(int64_t)std::stoull(s, nullptr, 0);
        return alloc_hv_emit(HVInt{v});
    }
    if (code == la::HERMES_FLOAT.code) {
        std::string s(str_of(node.get(la::VALUE.code)));
        double v = s.empty() ? 0.0 : std::stod(s);
        return alloc_hv_emit(HVFloat{v});
    }
    if (code == la::HERMES_BOOL.code) {
        auto av = node.get(la::VALUE.code);
        bool v = av.is_value() && av.as_value<uint8_t>() != 0;
        return alloc_hv_emit(HVBool{v});
    }
    if (code == la::HERMES_NULL.code) {
        return alloc_hv_emit(HVNull{});
    }
    if (code == la::HERMES_CAP_IDENT.code || code == la::HERMES_CAP_EXPR.code) {
        // Captures not allowed in static meta @{} blocks.
        return nullptr;
    }
    // Unknown node type — treat as null.
    return alloc_hv_emit(HVNull{});
}

// Extract meta_val from an AST node (struct/datatype/trait) if it has a META key.
lir::HermesValPtr SemaChecker::extract_meta_val(TinyMapView node) {
    if (!node.has_key(la::META)) return nullptr;
    auto mv = node.get(la::META.code);
    if (mv.is_null()) return nullptr;
    auto meta_node = map_of(mv);
    if (code_of(meta_node) != la::META_BLOCK.code) return nullptr;
    if (!meta_node.has_key(la::VALUE)) return nullptr;
    return eval_static_hermes_lit(map_of(meta_node.get(la::VALUE.code)));
}

lir::LFunction SemaChecker::lower_fn(TinyMapView node, std::string_view struct_ctx) {
    auto raw_name = str_of(node.get(la::NAME.code));
    std::string mangled = struct_ctx.empty()
        ? std::string(raw_name)
        : std::string(struct_ctx) + "__" + std::string(raw_name);

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

    // Some trait-default bodies and impl methods refer to `Self` in their
    // parameter types.  Keep a concrete Self binding alive for the duration
    // of lowering if the surrounding impl context already determined it.
    if (!struct_ctx.empty() && !current_type_params_.count("Self")) {
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

    lir::LFunction fn;
    fn.name               = mangled;
    fn.from_binary_module = cur_from_binary_;
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
    if (node.has_key(la::PARAMS)) {
        auto params_av = node.get(la::PARAMS.code);
        if (params_av.is_pointer()) {
                auto params_node = map_of(params_av);
                if (params_node.has_key(la::ITEMS)) {
                    auto arr = arr_of(params_node.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < arr.size(); ++i) {
                        auto p = map_of(arr.get(i));
                        if (code_of(p) != la::PARAM) continue;
                        if (p.has_key(la::TYPE))
                            decl_param_types.push_back(resolve_type(map_of(p.get(la::TYPE.code))));
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
    decl_param_arity = decl_param_types.size();
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
        }
    }
    if (!fi_ptr) return fn;   // shouldn't happen after collect

    fn.name           = fi_ptr->symbol_name.empty() ? mangled : fi_ptr->symbol_name;
    fn.type_params    = fi_ptr->type_params;
    fn.lifetime_params = read_lifetime_params(node);
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

    scope_.clear();
    push_scope();

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
                    auto pname = str_of(p.get(la::NAME.code));
                    auto ptype = (i < fi_ptr->param_types.size())
                        ? fi_ptr->param_types[i] : error_t();
                    bool p_variadic = false;
                    if (p.has_key(la::IS_VARIADIC)) {
                        AnyVal av = p.get(la::IS_VARIADIC.code);
                        p_variadic = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
                    }
                    // Simplify parameter type too
                    TypeRef pt = subst_type_sema(ptype, {});
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
                    define(pname, pt);
                    fn.params.push_back({std::string(pname), pt, p_variadic});
                }
            }
        }
    }

    // unsafe fn body is implicitly an unsafe context
    bool was_unsafe = inside_unsafe_;
    if (fi_ptr->is_unsafe) inside_unsafe_ = true;

    // Metaprog pre-sema mode: in the entry AST, skip body lowering for fns
    // that are NOT registered metaprog handlers. The entry file may reference
    // symbols (impls, types) that don't exist yet — they will be synthesized
    // by handler hooks. Handlers themselves must be fully lowered so the JIT
    // can compile them.
    bool skip_body = metaprog_mode_
                  && cur_ast_idx_ == metaprog_entry_ast_idx_
                  && !fn_is_metaprog_handler(fn.name);
    if (skip_body) fn.is_metaprog_stub = true;

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
        fn.body = lower_block(body_node);
        match_in_tail_position_ = false;
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
        // Return reachability check (on AST node — before scope is gone)
        if (fn.ret_type && TypeRef(fn.ret_type).kind() != LogosType::Kind::Void &&
            TypeRef(fn.ret_type).kind() != LogosType::Kind::Error &&
            !block_always_returns(body_node)) {
            error("not all paths return a value");
        }
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
    push_type_params(sd.type_params);
    for (auto& f : sinfo->fields)
        sd.fields.push_back({std::string(f.name), f.type, f.is_variadic});
    if (node.has_key(la::ITEMS)) {
        auto methods = arr_of(node.get(la::ITEMS.code));
        for (uint64_t m = 0; m < methods.size(); ++m) {
            auto method = map_of(methods.get(m));
            int32_t mc = code_of(method);
            if (mc == la::FN || mc == la::STATIC_FN)
                sd.methods.push_back(std::make_unique<lir::LFunction>(lower_fn(method, sname)));
        }
    }
    sd.meta_val = extract_meta_val(node);
    pop_type_params(sd.type_params);
    return sd;
}

lir::LEnumDef SemaChecker::lower_enum_def(TinyMapView node) {
    auto ename = std::string(str_of(node.get(la::NAME.code)));
    lir::LEnumDef ed;
    ed.name = ename;
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
    for (auto& v : einfo.variants)
        ed.variants.push_back({std::string(v.name), v.value, v.payload_types, v.is_variadic});
    return ed;
}


lir::LConst SemaChecker::lower_const_def(TinyMapView node) {
    auto name = std::string(str_of(node.get(la::NAME.code)));
    lir::LConst lc;
    lc.name = name;
    auto cit = module_consts_.find(name);
    lc.type = (cit != module_consts_.end()) ? cit->second : error_t();
    if (node.has_key(la::VALUE)) {
        lc.value = lower_expr(map_of(node.get(la::VALUE.code)));
    } else {
        lc.value = error_expr();
    }
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
            td.assoc_types.push_back({at.name, at.bounds});
        for (auto& m : tit->second.methods) {
            lir::LTraitMethodSig sig;
            sig.name     = m.name;
            sig.ret_type = m.ret_type;
            // We don't lower params here since they may contain Self
            td.methods.push_back(std::move(sig));
        }
    }
    td.pkg = std::string(cur_package_);
    // Bug 4 fix: only set is_auto here; is_genos is dead code since the caller
    // at sema.cpp:2338 overwrites it for GENOS_DEF, and TRAIT_DEF is always false.
    if (tit != traits_.end())
        td.is_auto = tit->second.is_auto;
    if (node.has_key(la::TYPE_PARAMS)) {
        auto tps = read_type_params(node);
        for (auto& tp : tps)
            td.type_params.push_back(tp.name);
    }
    td.meta_val = extract_meta_val(node);
    return td;
}

void SemaChecker::lower_impl_block(TinyMapView node, lir::LProgram& prog) {
    std::string trait_name;
    if (node.has_key(la::NAME))
        trait_name = std::string(str_of(node.get(la::NAME.code)));
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
            }
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
                for (uint64_t i = 0; i < items.size(); ++i) {
                    auto resolved = resolve_type(map_of(items.get(i)));
                    impl_trait_args.push_back(resolved);
                    if (tit != traits_.end() && i < tit->second.type_params.size())
                        current_type_params_[tit->second.type_params[i].name] = resolved;
                }
            }
        }
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
            for (auto& sd : prog.structs)
                if (sd.name == target) { target_struct_tmpl = &sd; break; }
        }
    }
    StrSet overridden;
    // Blanket impls lower methods under a synthetic target name so they don't
    // collide with `T::method` for any other generic `T` in the program.
    std::string lower_target = ib.is_blanket
        ? ("$blanket$" + trait_name + "$" + ib.bound_trait + "$" + target)
        : target;
    if (node.has_key(la::ITEMS)) {
        auto items = arr_of(node.get(la::ITEMS.code));
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto m = map_of(items.get(i));
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
                    prog.functions.push_back(std::make_unique<lir::LFunction>(std::move(fn)));
                }
            }
        }
    }
    // Lower default methods from the trait that weren't overridden.
    if (!trait_name.empty()) {
        auto tit = traits_.find(trait_name);
        if (tit != traits_.end()) {
            for (auto& m : tit->second.methods) {
                auto mangled = target + "__" + m.name;
                if (m.has_default && !overridden.count(mangled)) {
                    // Push Self → target type; for generic impls include type params as TypeVars.
                    TypeRef self_type = nullptr;
                    {
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
                        }
                    }
                    if (self_type)
                        current_type_params_["Self"] = self_type;
                    // Switch holder to the zone that owns the default AST node
                    // (may be a different module's zone for cross-module traits).
                    auto* saved_holder = holder_;
                    if (m.default_holder) holder_ = m.default_holder;
                    auto fn = lower_fn(map_of(m.default_ast), target);
                    holder_ = saved_holder;
                    fn.is_pub = true;  // default trait method inherits trait visibility
                    prog.functions.push_back(std::make_unique<lir::LFunction>(std::move(fn)));
                    current_type_params_.erase("Self");
                }
            }
        }
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
        auto prefix = trait_name + "::" + stored_target + "::";
        for (auto& [key, entry] : assoc_type_impls_) {
            if (key.rfind(prefix, 0) == 0) {
                auto assoc_name = key.substr(prefix.size());
                ib.assoc_types[assoc_name] = entry.type;
            }
        }
    }
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

                        lir::LDispatchEntry de;
                        de.tag_system     = tag_system;
                        de.trait_name     = trait_name;
                        de.method_name    = m.name;
                        de.fn_symbol      = mangled;
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
