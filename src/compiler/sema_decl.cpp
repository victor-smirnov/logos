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

lir::LFunction SemaChecker::lower_fn(TinyMapView node, std::string_view struct_ctx) {
    auto raw_name = str_of(node.get(la::NAME.code));
    std::string mangled = struct_ctx.empty()
        ? std::string(raw_name)
        : std::string(struct_ctx) + "__" + std::string(raw_name);

    ctx_       = std::format("fn {}", mangled);
    node_line_ = get_line(node);

    lir::LFunction fn;
    fn.name      = mangled;
    int32_t node_code = code_of(node);
    fn.is_extern = (node_code == la::EXTERN_FN);

    // Check is_vararg for extern fn with variadic params
    if (fn.is_extern && node.has_key(la::IS_VARARG)) {
        AnyVal av = node.get(la::IS_VARARG.code);
        fn.is_vararg = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }

    SemaFuncInfo* fi_ptr = nullptr;
    {
        // If this AST node actually has type params, prefer generic_funcs_.
        auto node_tparams = read_type_params(node);
        if (!node_tparams.empty()) {
            auto git = generic_funcs_.find(mangled);
            if (git != generic_funcs_.end()) fi_ptr = &git->second;
        }
        if (!fi_ptr) {
            auto it = funcs_.find(mangled);
            if (it != funcs_.end()) fi_ptr = &it->second;
        }
        if (!fi_ptr) {
            auto git = generic_funcs_.find(mangled);
            if (git != generic_funcs_.end()) fi_ptr = &git->second;
        }
    }
    if (!fi_ptr) return fn;   // shouldn't happen after collect

    fn.type_params    = fi_ptr->type_params;
    fn.lifetime_params = read_lifetime_params(node);
    // Robust associated type resolution: call subst_type_sema even if subst is empty
    // to simplify concrete AssocType nodes (e.g. i32::Item -> bool).
    fn.ret_type    = subst_type_sema(fi_ptr->ret_type, {});
    ret_type_      = fn.ret_type;
    // Bug 5 fix: DataNode enforcement covers both bare Datatype and Array-of-Datatype
    // return/param types.  Extract the innermost non-Array element for the check.
    auto datanode_name = [&](const LogosType* t) -> std::string {
        if (!t) return {};
        while (t->kind == LogosType::Kind::Array) t = t->elem;
        if (t->kind == LogosType::Kind::Datatype && t->type_args.empty()) {
            auto dit = datatypes_.find(t->struct_name);
            if (dit != datatypes_.end() && !dit->second.is_data_plain)
                return t->struct_name;
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
    if (fn.ret_type && fn.ret_type->kind == LogosType::Kind::ImplTrait)
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
                    const LogosType* pt = subst_type_sema(ptype, {});
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

    // Body (extern fns have no body)
    if (!fn.is_extern && node.has_key(la::BODY)) {
        auto body_node = map_of(node.get(la::BODY.code));
        // Detect if the last stmt in the function body is a match.
        // If so, set the flag so lower_match treats EXPR arms as return values.
        if (fn.ret_type && fn.ret_type->kind != LogosType::Kind::Void) {
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
        if (fn.ret_type && fn.ret_type->kind == LogosType::Kind::ImplTrait) {
            if (impl_ret_type_inferred_) {
                fn.ret_type       = impl_ret_type_inferred_;
                fi_ptr->ret_type  = impl_ret_type_inferred_;
                ret_type_         = impl_ret_type_inferred_;
            } else {
                error("impl Trait return: could not infer concrete return type");
            }
        }
        // Return reachability check (on AST node — before scope is gone)
        if (fn.ret_type && fn.ret_type->kind != LogosType::Kind::Void &&
            fn.ret_type->kind != LogosType::Kind::Error &&
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
    sd.name = sname;
    // Look up in structs_ or datatypes_ — never default-insert via operator[].
    auto sit  = structs_.find(sname);
    auto doit = datatypes_.find(sname);
    SemaStructInfo* sinfo = nullptr;
    if      (sit  != structs_.end())   sinfo = &sit->second;
    else if (doit != datatypes_.end()) sinfo = &doit->second;
    else {
        error(std::format("internal: '{}' not found in collect phase", sname));
        return sd;
    }
    sd.type_params = sinfo->type_params;
    push_type_params(sd.type_params);
    for (auto& f : sinfo->fields)
        sd.fields.push_back({std::string(f.name), f.type, f.is_variadic});
    if (node.has_key(la::ITEMS)) {
        auto methods = arr_of(node.get(la::ITEMS.code));
        for (uint64_t m = 0; m < methods.size(); ++m) {
            auto method = map_of(methods.get(m));
            int32_t mc = code_of(method);
            if (mc == la::FN || mc == la::STATIC_FN)
                sd.methods.push_back(lower_fn(method, sname));
        }
    }
    pop_type_params(sd.type_params);
    return sd;
}

lir::LEnumDef SemaChecker::lower_enum_def(TinyMapView node) {
    auto ename = std::string(str_of(node.get(la::NAME.code)));
    lir::LEnumDef ed;
    ed.name = ename;
    auto& einfo = enums_[ename];
    ed.type_params = einfo.type_params;
    for (auto& v : einfo.variants)
        ed.variants.push_back({std::string(v.name), v.value, v.payload_types, v.is_variadic});
    return ed;
}

std::optional<int64_t> SemaChecker::const_eval_expr(TinyMapView e, const ConstEnv& env) {
    if (!e.ptr()) return std::nullopt;
    auto c = code_of(e);

    if (c == la::LIT_INT) {
        auto sv = str_of(e.get(la::VALUE.code));
        return parse_int_literal(sv);
    }
    if (c == la::LIT_BOOL) {
        auto sv = str_of(e.get(la::VALUE.code));
        return sv == "true" ? (int64_t)1 : (int64_t)0;
    }
    if (c == la::VAR_REF) {
        auto name = str_of(e.get(la::NAME.code));
        auto it = env.find(std::string(name));
        return it != env.end() ? std::optional<int64_t>(it->second) : std::nullopt;
    }
    if (c == la::PAREN_EXPR)
        return const_eval_expr(map_of(e.get(la::VALUE.code)), env);
    if (c == la::CAST)
        return const_eval_expr(map_of(e.get(la::VALUE.code)), env);
    if (c == la::UNARY) {
        auto op  = str_of(e.get(la::OP.code));
        auto val = const_eval_expr(map_of(e.get(la::VALUE.code)), env);
        if (!val) return std::nullopt;
        if (op == "-") return -*val;
        if (op == "!") return *val ? (int64_t)0 : (int64_t)1;
        return std::nullopt;
    }
    if (c == la::BINOP) {
        auto op = str_of(e.get(la::OP.code));
        auto l  = const_eval_expr(map_of(e.get(la::LHS.code)), env);
        auto r  = const_eval_expr(map_of(e.get(la::RHS.code)), env);
        if (!l || !r) return std::nullopt;
        if (op == "+")  return *l + *r;
        if (op == "-")  return *l - *r;
        if (op == "*")  return *l * *r;
        if (op == "/" && *r != 0) return *l / *r;
        if (op == "%" && *r != 0) return *l % *r;
        if (op == "<")  return *l < *r  ? (int64_t)1 : (int64_t)0;
        if (op == ">")  return *l > *r  ? (int64_t)1 : (int64_t)0;
        if (op == "<=") return *l <= *r ? (int64_t)1 : (int64_t)0;
        if (op == ">=") return *l >= *r ? (int64_t)1 : (int64_t)0;
        if (op == "==") return *l == *r ? (int64_t)1 : (int64_t)0;
        if (op == "!=") return *l != *r ? (int64_t)1 : (int64_t)0;
        if (op == "&&") return (*l && *r) ? (int64_t)1 : (int64_t)0;
        if (op == "||") return (*l || *r) ? (int64_t)1 : (int64_t)0;
        return std::nullopt;
    }
    if (c == la::IF) {
        auto cond = e.has_key(la::COND)
            ? const_eval_expr(map_of(e.get(la::COND.code)), env)
            : std::nullopt;
        if (!cond) return std::nullopt;
        AnyVal branch = *cond ? e.get(la::THEN.code) : e.get(la::ELSE.code);
        if (branch.is_null()) return std::nullopt;
        return const_eval_block(map_of(branch), env);
    }
    // Nested const fn call: CALL node with a known const fn callee.
    if (c == la::CALL) {
        auto callee_name = str_of(e.get(la::CALLEE.code));
        auto fit = funcs_.find(std::string(callee_name));
        if (fit == funcs_.end() || !fit->second.is_const) return std::nullopt;
        auto bit = const_fn_bodies_.find(std::string(callee_name));
        if (bit == const_fn_bodies_.end()) return std::nullopt;
        ConstEnv call_env;
        if (e.has_key(la::ARGS)) {
            auto args_arr = arr_of(e.get(la::ARGS.code));
            auto& pnames = bit->second.param_names;
            if (args_arr.size() != pnames.size()) return std::nullopt;
            for (uint64_t i = 0; i < args_arr.size(); ++i) {
                auto av = const_eval_expr(map_of(args_arr.get(i)), env);
                if (!av) return std::nullopt;
                call_env[pnames[i]] = *av;
            }
        }
        return const_eval_block(bit->second.body, call_env);
    }
    return std::nullopt;
}

std::optional<int64_t> SemaChecker::const_eval_block(TinyMapView block, ConstEnv env) {
    if (!block.ptr() || !block.has_key(la::ITEMS)) return std::nullopt;
    auto stmts = arr_of(block.get(la::ITEMS.code));
    for (uint64_t i = 0; i < stmts.size(); ++i) {
        auto stmt = map_of(stmts.get(i));
        auto c = code_of(stmt);
        if (c == la::RETURN) {
            if (stmt.has_key(la::VALUE))
                return const_eval_expr(map_of(stmt.get(la::VALUE.code)), env);
            return (int64_t)0;
        }
        if (c == la::LET) {
            auto name = str_of(stmt.get(la::NAME.code));
            if (!stmt.has_key(la::VALUE)) return std::nullopt;
            auto v = const_eval_expr(map_of(stmt.get(la::VALUE.code)), env);
            if (!v) return std::nullopt;
            env[std::string(name)] = *v;
            continue;
        }
        if (c == la::IF) {
            auto cond = stmt.has_key(la::COND)
                ? const_eval_expr(map_of(stmt.get(la::COND.code)), env)
                : std::nullopt;
            if (!cond) return std::nullopt;
            if (*cond) {
                auto res = const_eval_block(map_of(stmt.get(la::THEN.code)), env);
                if (res) return res;  // returned from branch
            } else if (stmt.has_key(la::ELSE)) {
                auto res = const_eval_block(map_of(stmt.get(la::ELSE.code)), env);
                if (res) return res;
            }
            continue;
        }
        // Other statements not supported in const context — give up
        return std::nullopt;
    }
    return std::nullopt;
}

lir::LExprPtr SemaChecker::try_const_fold_call(TinyMapView call_node) {
    if (!call_node.ptr() || code_of(call_node) != la::CALL) return nullptr;
    auto callee_name = str_of(call_node.get(la::CALLEE.code));
    auto fit = funcs_.find(std::string(callee_name));
    if (fit == funcs_.end() || !fit->second.is_const) return nullptr;
    auto bit = const_fn_bodies_.find(std::string(callee_name));
    if (bit == const_fn_bodies_.end()) return nullptr;

    // Evaluate arguments as constants.
    ConstEnv env;
    if (call_node.has_key(la::ARGS)) {
        auto args_arr = arr_of(call_node.get(la::ARGS.code));
        auto& pnames = bit->second.param_names;
        if (args_arr.size() != pnames.size()) return nullptr;
        for (uint64_t i = 0; i < args_arr.size(); ++i) {
            auto arg_val = const_eval_expr(map_of(args_arr.get(i)), {});
            if (!arg_val) return nullptr;
            env[pnames[i]] = *arg_val;
        }
    }

    auto result = const_eval_block(bit->second.body, env);
    if (!result) return nullptr;
    return make_expr(i32_t(), lir::ELitInt{*result});
}

lir::LConst SemaChecker::lower_const_def(TinyMapView node) {
    auto name = std::string(str_of(node.get(la::NAME.code)));
    lir::LConst lc;
    lc.name = name;
    auto cit = module_consts_.find(name);
    lc.type = (cit != module_consts_.end()) ? cit->second : error_t();
    if (node.has_key(la::VALUE)) {
        auto val_node = map_of(node.get(la::VALUE.code));
        // Try to fold const fn calls at compile time.
        if (auto folded = try_const_fold_call(val_node)) {
            lc.value = std::move(folded);
        } else {
            lc.value = lower_expr(val_node);
        }
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
    if (node.has_key(la::TYPE)) {
        auto tnode = map_of(node.get(la::TYPE.code));
        if (code_of(tnode) == la::PTR_TYPE) {
            auto* resolved = resolve_type(tnode);
            target = type_str(resolved);
        } else if (code_of(tnode) == la::GENERIC_INST) {
            target = std::string(str_of(tnode.get(la::NAME.code)));
            if (impl_tps.empty()) {
                auto* resolved = resolve_type(tnode);
                if (resolved && !resolved->type_args.empty()) {
                    bool concrete = true;
                    for (auto* a : resolved->type_args)
                        if (a && a->kind == LogosType::Kind::TypeVar) { concrete = false; break; }
                    if (concrete) {
                        if (resolved->kind == LogosType::Kind::Struct)
                            target = concrete_struct_name(resolved);
                    }
                }
            }
        } else {
            target = std::string(str_of(tnode.get(la::NAME.code)));
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
                break;
            }
            break;
        }
    }
    // Blanket impl detection: target IS one of this impl's own type parameters.
    if (!trait_name.empty()) {
        for (auto& tp : impl_tps) {
            if (tp.name == target) {
                ib.is_blanket = true;
                if (!tp.bounds.empty())
                    ib.bound_trait = tp.bounds[0].trait_name;
                break;
            }
        }
    }
    if (node.has_key(la::IS_UNSAFE)) {
        AnyVal av = node.get(la::IS_UNSAFE);
        ib.is_unsafe = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }
    // Resolve trait type args and push into scope
    std::vector<const LogosType*> impl_trait_args;
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
    lir::LStructDef* target_struct_tmpl = nullptr;
    if (!impl_tps.empty()) {
        for (auto& sd : prog.structs)
            if (sd.name == target) { target_struct_tmpl = &sd; break; }
    }
    std::unordered_set<std::string> overridden;
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
                if (target_struct_tmpl) {
                    // Add to struct template so mono clones it during struct instantiation.
                    fn.type_params.clear();
                    target_struct_tmpl->methods.push_back(std::move(fn));
                } else {
                    prog.functions.push_back(std::move(fn));
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
                    const LogosType* self_type = nullptr;
                    if (structs_.count(target)) {
                        if (!impl_tps.empty()) {
                            std::vector<const LogosType*> tv_args;
                            for (auto& tp : impl_tps)
                                tv_args.push_back(make_typevar(tp.name));
                            self_type = make_generic_struct(target, std::move(tv_args));
                        } else {
                            self_type = make_struct_type(target);
                        }
                    }
                    if (self_type)
                        current_type_params_["Self"] = self_type;
                    auto fn = lower_fn(map_of(m.default_ast), target);
                    fn.is_pub = true;  // default trait method inherits trait visibility
                    prog.functions.push_back(std::move(fn));
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
                if (sd.is_datatype && sd.name == target && sd.type_code != 0) {
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
                    auto dit = datatypes_.find(target);
                    if (dit != datatypes_.end()) {
                        auto foreign_fqn = dit->second.package + "::" + target;
                        eit = explicit_type_codes_.find(foreign_fqn);
                    }
                }
                if (eit != explicit_type_codes_.end()) {
                    tcode = eit->second;
                } else {
                    auto dit = datatypes_.find(target);
                    if (dit != datatypes_.end()) {
                        std::string canon = dit->second.package + "::" + target;
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
