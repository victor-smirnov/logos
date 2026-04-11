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

// Symbol-collection phase: populate SemaChecker symbol tables.

void SemaChecker::collect(const std::vector<hermes::HermesCtr>& asts) {
    // First pass: register names (so forward references work).
    for (auto& ast : asts) {
        holder_ = ast.holder();
        auto root = ast.root_object().as_tiny_map();
        if (!root.has_key(la::ITEMS)) continue;
        auto items = arr_of(root.get(la::ITEMS.code));
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto item = map_of(items.get(i));
            int32_t ic = code_of(item);
            if (ic == la::STRUCT) {
                if (is_specialization_struct(item)) continue;  // specs registered later
                auto sname = std::string(str_of(item.get(la::NAME.code)));
                if (structs_.count(sname)) error(std::format("duplicate struct '{}'", sname));
                else structs_[sname] = {};
            } else if (ic == la::ENUM) {
                auto ename = std::string(str_of(item.get(la::NAME.code)));
                if (enums_.count(ename)) error(std::format("duplicate enum '{}'", ename));
                else enums_[ename] = {};
            } else if (ic == la::CLASS) {
                auto cname = std::string(str_of(item.get(la::NAME.code)));
                if (classes_.count(cname)) error(std::format("duplicate class '{}'", cname));
                else classes_[cname] = {};
            }
        }
    }
    // Intermediate pass: type aliases and consts (Phase 2). Wait, we execute this FIRST so aliases are known for fn signatures.
    for (size_t ai = 0; ai < asts.size(); ++ai) {
        holder_ = asts[ai].holder();
        file_ = (filenames_ && ai < filenames_->size()) ? (*filenames_)[ai] : std::string{};
        auto root = asts[ai].root_object().as_tiny_map();
        cur_package_ = read_package_name(root);
        collect_module(root, 2);
    }
    // Second pass: fill in fields, variants, function signatures (Phase 1).
    for (size_t ai = 0; ai < asts.size(); ++ai) {
        holder_ = asts[ai].holder();
        file_ = (filenames_ && ai < filenames_->size()) ? (*filenames_)[ai] : std::string{};
        auto root = asts[ai].root_object().as_tiny_map();
        cur_package_ = read_package_name(root);
        collect_module(root, 1);
    }
    cur_package_ = {};

    // Third pass: build class inheritance (all_fields + full vtable_order).
    finalize_classes();

    // Final pass: simplify all collected types (resolve concrete associated types).
    simplify_all_types();
}

void SemaChecker::simplify_all_types() {
    for (auto& [name, info] : structs_) {
        for (auto& f : info.fields) f.type = subst_type_sema(f.type, {});
    }
    for (auto& [name, info] : classes_) {
        for (auto& f : info.all_fields) f.type = subst_type_sema(f.type, {});
    }
    for (auto& [name, info] : enums_) {
        for (auto& v : info.variants) {
            for (auto& pt : v.payload_types) pt = subst_type_sema(pt, {});
        }
    }
    auto simplify_fn = [&](SemaFuncInfo& fi) {
        for (auto& pt : fi.param_types) pt = subst_type_sema(pt, {});
        fi.ret_type = subst_type_sema(fi.ret_type, {});
    };
    for (auto& [name, info] : funcs_) simplify_fn(info);
    for (auto& [name, info] : generic_funcs_) simplify_fn(info);
    for (auto& [name, t] : type_aliases_)   t = subst_type_sema(t, {});
    for (auto& [name, t] : module_consts_)   t = subst_type_sema(t, {});
}

std::string SemaChecker::read_package_name(TinyMapView mod) {
    if (!mod.has_key(la::NAME)) return {};
    return std::string(str_of(mod.get(la::NAME.code)));
}

void SemaChecker::check_pub_access(bool is_pub, const std::string& def_package,
                          std::string_view item_name) {
    if (is_pub || def_package.empty() || cur_package_.empty()) return;
    if (def_package != cur_package_)
        error(std::format("'{}' is private to package '{}'", item_name, def_package));
}

void SemaChecker::check_type_bounds(const std::string& target_name,
                           const std::vector<TypeParam>& type_params,
                           const std::vector<const LogosType*>& args) {
    if (type_params.empty()) return;
    bool has_variadic = type_params.back().is_variadic;
    size_t non_variadic_count = type_params.size() - (has_variadic ? 1 : 0);

    for (size_t i = 0; i < args.size(); ++i) {
        if (i >= type_params.size() && !has_variadic) break;

        const auto& tp = (has_variadic && i >= non_variadic_count)
                         ? type_params.back()
                         : type_params[i];

        auto* concrete = args[i];
        if (!concrete || concrete->kind == LogosType::Kind::Error) continue;
        if (concrete->kind == LogosType::Kind::TypeVar) continue; // defer until mono

        std::string concrete_str = type_str(concrete);
        std::string unwrapped_name;
        if ((concrete->kind == LogosType::Kind::Ptr || concrete->kind == LogosType::Kind::Ref || concrete->kind == LogosType::Kind::MutRef) && concrete->pointee) {
            auto* inner = concrete->pointee;
            if (inner->kind == LogosType::Kind::Class)
                unwrapped_name = concrete_class_name(inner);
            else if (inner->kind == LogosType::Kind::Struct)
                unwrapped_name = concrete_struct_name(inner);
        } else if (concrete->kind == LogosType::Kind::Struct) {
            unwrapped_name = concrete_struct_name(concrete);
        } else if (concrete->kind == LogosType::Kind::Class) {
            unwrapped_name = concrete_class_name(concrete);
        }

        for (auto& bound : tp.bounds) {
            auto key1 = bound.trait_name + "::" + concrete_str;
            auto key2 = unwrapped_name.empty() ? "" : bound.trait_name + "::" + unwrapped_name;
            if (!impls_.count(key1) && (key2.empty() || !impls_.count(key2))) {
                error(std::format("'{}': type '{}' does not implement trait '{}' required by parameter '{}'",
                      target_name, concrete_str, bound.trait_name, tp.name));
            }
        }
    }
}

void SemaChecker::collect_module(TinyMapView mod, int phase) {
    if (!mod.has_key(la::ITEMS)) return;
    auto items = arr_of(mod.get(la::ITEMS.code));
    for (uint64_t i = 0; i < items.size(); ++i) {
        auto item = map_of(items.get(i));
        int32_t c = code_of(item);
        if (phase == 1) {
            if      (c == la::STRUCT) {
                if (is_specialization_struct(item)) collect_struct_spec(item);
                else                                collect_struct(item);
            } else if (c == la::ENUM)                       collect_enum(item);
            else if (c == la::CLASS)                        collect_class(item);
            else if (c == la::FN || c == la::EXTERN_FN)   collect_fn(item);
            else if (c == la::TRAIT_DEF)                  collect_trait(item);
            else if (c == la::IMPL_BLOCK)                 collect_impl(item);
        } else {
            if      (c == la::TYPE_ALIAS)                 collect_type_alias(item);
            else if (c == la::CONST_DEF)                  collect_const(item);
        }
    }
}

void SemaChecker::collect_enum(TinyMapView node) {
    auto ename = std::string(str_of(node.get(la::NAME.code)));
    ctx_ = std::format("enum {}", ename);
    SemaEnumInfo info;
    info.type_params = read_type_params(node);
    push_type_params(info.type_params);
    int32_t next_val = 0;
    if (node.has_key(la::ITEMS)) {
        auto av = node.get(la::ITEMS.code);
        if (av.is_pointer()) {
            auto list = map_of(av);
            if (list.has_key(la::ITEMS)) {
                auto variants = arr_of(list.get(la::ITEMS.code));
                for (uint64_t i = 0; i < variants.size(); ++i) {
                    auto v = map_of(variants.get(i));
                    auto vname = str_of(v.get(la::NAME.code));
                    int32_t vval = next_val;
                    if (v.has_key(la::VALUE)) {
                        auto sv = str_of(v.get(la::VALUE.code));
                        vval = (int32_t)parse_int_literal(sv);
                    }
                    // Read payload types for tagged union variants
                    std::vector<const LogosType*> payload;
                    bool is_var = false;
                    if (v.has_key(la::IS_VARIADIC)) is_var = v.get(la::IS_VARIADIC.code).as_value<int32_t>() != 0;

                    if (v.has_key(la::ITEMS)) {
                        auto av = v.get(la::ITEMS.code);
                        if (is_var) {
                            // Single type_ref map (variadic variant: ITEMS: $4)
                            payload.push_back(resolve_type(map_of(av)));
                        } else {
                            // Nested record { ITEMS: [...] } (normal variant: ITEMS: $3)
                            // or raw array (old grammar/other paths)
                            TinyMapView tm(av.to_offset(), holder_);
                            ArrayView pitems;
                            if (tm.has_key(la::ITEMS)) {
                                pitems = arr_of(tm.get(la::ITEMS.code));
                            } else {
                                pitems = arr_of(av);
                            }
                            for (uint64_t j = 0; j < pitems.size(); ++j)
                                payload.push_back(resolve_type(map_of(pitems.get(j))));
                        }
                    }
                    info.variants.push_back({vname, vval, std::move(payload), is_var});
                    next_val = vval + 1;
                }
            }
        }
    }
    pop_type_params(info.type_params);
    enums_[ename] = std::move(info);
}

void SemaChecker::collect_type_alias(TinyMapView node) {
    auto name = std::string(str_of(node.get(la::NAME.code)));
    if (!node.has_key(la::TYPE)) return;
    type_aliases_[name] = resolve_type(map_of(node.get(la::TYPE.code)));
}

void SemaChecker::collect_const(TinyMapView node) {
    auto name = std::string(str_of(node.get(la::NAME.code)));
    const LogosType* t = nullptr;
    if (node.has_key(la::TYPE)) {
        t = resolve_type(map_of(node.get(la::TYPE.code)));
    } else if (node.has_key(la::VALUE)) {
        // Evaluate type of the value expression lazily.
        // We just store an i32 as placeholder for now.
        t = i32_t();
    }
    if (t) module_consts_[name] = t;
}

void SemaChecker::collect_trait(TinyMapView node) {
    auto tname = std::string(str_of(node.get(la::NAME.code)));
    ctx_ = std::format("trait {}", tname);
    // Push "Self" as a type param so trait method signatures can reference it.
    current_type_params_["Self"] = make_typevar("Self");
    current_trait_name_ = tname;
    SemaTraitInfo info;
    info.name = tname;
    // Read trait type params (e.g. trait Into<T>)
    info.type_params = read_type_params(node);
    push_type_params(info.type_params);
    if (node.has_key(la::ITEMS)) {
        auto items = arr_of(node.get(la::ITEMS.code));
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto m = map_of(items.get(i));
            if (code_of(m) == la::ASSOC_TYPE_DEF) {
                SemaAssocTypeInfo at;
                at.name = std::string(str_of(m.get(la::NAME.code)));
                if (m.has_key(la::ITEMS)) {
                    auto bounds = arr_of(m.get(la::ITEMS.code));
                    for (uint64_t b = 0; b < bounds.size(); ++b) {
                        auto bnode = map_of(bounds.get(b));
                        if (code_of(bnode) == la::TRAIT_BOUND) {
                            TraitBound tb;
                            tb.trait_name = std::string(str_of(bnode.get(la::NAME.code)));
                            at.bounds.push_back(std::move(tb));
                        }
                    }
                }
                info.assoc_types.push_back(std::move(at));
                continue;
            }
            if (code_of(m) != la::FN) continue;
            SemaTraitMethodInfo mi;
            mi.name = std::string(str_of(m.get(la::NAME.code)));
            if (m.has_key(la::PARAMS)) {
                auto pav = m.get(la::PARAMS.code);
                if (!pav.is_null() && pav.is_pointer()) {
                    auto plist = map_of(pav);
                    if (plist.has_key(la::ITEMS)) {
                        auto params = arr_of(plist.get(la::ITEMS.code));
                        for (uint64_t j = 0; j < params.size(); ++j) {
                            auto p = map_of(params.get(j));
                            if (p.has_key(la::TYPE))
                                mi.param_types.push_back(resolve_type(map_of(p.get(la::TYPE.code))));
                        }
                    }
                }
            }
            mi.ret_type = m.has_key(la::RET_TYPE)
                ? resolve_type(map_of(m.get(la::RET_TYPE.code))) : void_t();
            mi.has_default = m.has_key(la::BODY);
            if (mi.has_default)
                mi.default_ast = items.get(i);
            if (m.has_key(la::IS_UNSAFE)) {
                AnyVal av = m.get(la::IS_UNSAFE.code);
                mi.is_unsafe = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
            }
            info.methods.push_back(std::move(mi));
        }
    }
    pop_type_params(info.type_params);
    current_type_params_.erase("Self");
    current_trait_name_.clear();
    traits_[tname] = std::move(info);
}

void SemaChecker::collect_impl(TinyMapView node) {
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
        impl_type_params_ = impl_tps;  // so collect_fn includes them in fn.type_params
    }
    // TYPE is the target type (simple_type, ptr_type, or GENERIC_INST)
    std::string target;
    const LogosType* target_resolved = nullptr;  // concrete resolved type (for Self)
    if (node.has_key(la::TYPE)) {
        auto tnode = map_of(node.get(la::TYPE.code));
        if (code_of(tnode) == la::PTR_TYPE) {
            // *const T or *mut T → resolve full type string
            auto* resolved = resolve_type(tnode);
            target = type_str(resolved);
        } else if (code_of(tnode) == la::GENERIC_INST) {
            // Concrete generic (e.g. Pair<i32>) → use mangled name; generic (Pair<T>) → base name.
            target = std::string(str_of(tnode.get(la::NAME.code)));
            if (impl_tps.empty()) {
                // No own type params — may be a concrete specialization like impl Pair<i32>.
                auto* resolved = resolve_type(tnode);
                if (resolved && !resolved->type_args.empty()) {
                    bool concrete = true;
                    for (auto* a : resolved->type_args)
                        if (a && a->kind == LogosType::Kind::TypeVar) { concrete = false; break; }
                    if (concrete) {
                        if (resolved->kind == LogosType::Kind::Struct)
                            target = concrete_struct_name(resolved);
                        else if (resolved->kind == LogosType::Kind::Class)
                            target = concrete_class_name(resolved);
                        target_resolved = resolved;
                    }
                }
            }
        } else {
            target = std::string(str_of(tnode.get(la::NAME.code)));
        }
    }
    // Note: impl_tps are left in current_type_params_ until after collect_fn calls below.
    if (trait_name.empty())
        ctx_ = std::format("impl {}", target);
    else
        ctx_ = std::format("impl {} for {}", trait_name, target);
    // Set Self → the concrete target type so method signatures resolve *const Self, etc.
    // For generic impl<T> Foo<T>: Self = Foo<T> (TypeVars); for impl Foo<i32>: Self = Foo<i32>.
    {
        const LogosType* self_type = nullptr;
        if (target_resolved) {
            // Concrete specialization: use the fully resolved type (preserves type_args).
            self_type = target_resolved;
        } else {
            std::string base_target = target;
            if (auto d = base_target.find('$'); d != std::string::npos)
                base_target = base_target.substr(0, d);
            if (structs_.count(base_target) || structs_.count(target)) {
                std::string sname = structs_.count(target) ? target : base_target;
                if (!impl_tps.empty()) {
                    std::vector<const LogosType*> tv_args;
                    for (auto& tp : impl_tps)
                        tv_args.push_back(make_typevar(tp.name));
                    self_type = make_generic_struct(sname, std::move(tv_args));
                } else {
                    self_type = make_struct_type(target);
                }
            } else if (classes_.count(base_target) || classes_.count(target)) {
                std::string cname = classes_.count(target) ? target : base_target;
                self_type = make_ptr(true, make_class_type(cname));
            }
        }
        if (self_type)
            current_type_params_["Self"] = self_type;
    }
    // Verify trait exists (only for trait impls)
    // Copy is a built-in marker trait — not declared in source but always valid.
    if (!trait_name.empty() && trait_name != "Copy" && !traits_.count(trait_name))
        error(std::format("impl: unknown trait '{}'", trait_name));
    // Resolve trait type args (e.g. impl Into<i32> for Celsius → T=i32)
    // and push them into current_type_params_ so method sigs resolve correctly.
    std::vector<const LogosType*> trait_type_args;
    if (!trait_name.empty() && node.has_key(la::TYPE_PARAMS)) {
        AnyVal tpav = node.get(la::TYPE_PARAMS.code);
        if (!tpav.is_null()) {
            auto tplist = map_of(tpav);
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i)
                    trait_type_args.push_back(resolve_type(map_of(items.get(i))));
            }
        }
        auto tit = traits_.find(trait_name);
        if (tit != traits_.end()) {
            for (size_t i = 0; i < tit->second.type_params.size() &&
                                i < trait_type_args.size(); ++i)
                current_type_params_[tit->second.type_params[i].name] = trait_type_args[i];
        }
    }
    // Register impl methods as free functions with mangled names: Target__method
    // Also collect associated type definitions.
    // Skip if already registered (e.g. class methods defined inline).
    if (node.has_key(la::ITEMS)) {
        auto items = arr_of(node.get(la::ITEMS.code));
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto m = map_of(items.get(i));
            if (code_of(m) == la::FN || code_of(m) == la::STATIC_FN) {
                auto mname = std::string(str_of(m.get(la::NAME.code)));
                auto mangled = target + "__" + mname;
                if (!funcs_.count(mangled))
                    collect_fn(m, target);
            } else if (code_of(m) == la::ASSOC_TYPE_IMPL && !trait_name.empty()) {
                auto aname = std::string(str_of(m.get(la::NAME.code)));
                auto* atype = resolve_type(map_of(m.get(la::TYPE.code)));
                std::string key = trait_name + "::" + target + "::" + aname;
                assoc_type_impls_[key] = { atype, impl_tps };
            }
        }
    }
    // Check completeness: every required trait method must be in the impl.
    // Default methods are registered as Target__method if not overridden.
    if (!trait_name.empty()) {
        auto tit = traits_.find(trait_name);
        if (tit != traits_.end()) {
            for (auto& m : tit->second.methods) {
                auto mangled = target + "__" + m.name;
                if (funcs_.count(mangled)) {
                    auto& impl_fn = funcs_[mangled];
                    if (m.is_unsafe != impl_fn.is_unsafe) {
                        error(std::format("impl {} for {}: method '{}' has mismatched unsafe parity (trait: {}, impl: {})",
                            trait_name, target, m.name, 
                            m.is_unsafe ? "unsafe" : "safe", 
                            impl_fn.is_unsafe ? "unsafe" : "safe"));
                    }
                }
                if (!funcs_.count(mangled)) {
                    if (m.has_default) {
                        // Register default method as Target__method.
                        // Push Self → target type so parameter types resolve correctly.
                        // Build Self type; for generic impls include the type params as TypeVars.
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
                        } else if (classes_.count(target)) {
                            self_type = make_ptr(true, make_class_type(target));
                        }
                        if (self_type)
                            current_type_params_["Self"] = self_type;
                        collect_fn(map_of(m.default_ast), target);
                        current_type_params_.erase("Self");
                    } else {
                        error(std::format("impl {} for {}: missing method '{}'",
                              trait_name, target, m.name));
                    }
                }
            }
        }
    }
    // Check associated type completeness
    if (!trait_name.empty()) {
        auto tit = traits_.find(trait_name);
        if (tit != traits_.end()) {
            for (auto& at : tit->second.assoc_types) {
                std::string key = trait_name + "::" + target + "::" + at.name;
                if (!assoc_type_impls_.count(key))
                    error(std::format("impl {} for {}: missing associated type '{}'",
                          trait_name, target, at.name));
            }
        }
    }
    // Register Copy types so is_move_type() can respect them.
    if (trait_name == "Copy" && !target.empty())
        copy_types_.insert(target);
    // Clean up trait type params from scope
    if (!trait_name.empty() && !trait_type_args.empty()) {
        auto tit = traits_.find(trait_name);
        if (tit != traits_.end()) {
            for (auto& tp : tit->second.type_params)
                current_type_params_.erase(tp.name);
        }
    }
    // Clean up Self (set at top for all impl blocks)
    current_type_params_.erase("Self");
    // Clean up impl's own type params (pushed at top for standalone generic impl)
    if (!impl_tps.empty()) { pop_type_params(impl_tps); impl_type_params_.clear(); }
    // Register the impl mapping (only for trait impls)
    if (!trait_name.empty())
        impls_[trait_name + "::" + target] = {trait_name, target};
}

// Collect a struct specialization into struct_specs_sema_.
// Only full specializations (all patterns concrete) are registered;

void SemaChecker::collect_struct_spec(TinyMapView node) {
    auto sname = std::string(str_of(node.get(la::NAME.code)));
    ctx_ = std::format("struct {} (specialization)", sname);

    // Parse spec patterns to determine the concrete name.
    std::vector<const LogosType*> spec_patterns;
    std::vector<TypeParam> pattern_tvars;
    if (node.has_key(la::TYPE_PARAMS)) {
        AnyVal tpav = node.get(la::TYPE_PARAMS.code);
        if (!tpav.is_null()) {
            auto tplist = map_of(tpav);
            if (tplist.has_key(la::ITEMS)) {
                auto tpitems = arr_of(tplist.get(la::ITEMS.code));
                for (uint64_t i = 0; i < tpitems.size(); ++i) {
                    auto tpnode = map_of(tpitems.get(i));
                    int32_t tc  = code_of(tpnode);
                    if (tc == la::PTR_TYPE || tc == la::ARR_TYPE) {
                        extract_typevars_from_type_node(tpnode, pattern_tvars);
                        spec_patterns.push_back(resolve_type(tpnode));
                    } else if (tc == la::TYPE_PARAM) {
                        auto name = str_of(tpnode.get(la::NAME.code));
                        auto* known = try_resolve_as_known_type(name);
                        if (known) {
                            spec_patterns.push_back(known);
                        } else {
                            // Partial spec — skip for sema registration.
                            current_type_params_[std::string(name)] = make_typevar(name);
                            pattern_tvars.push_back({std::string(name), {}});
                            spec_patterns.push_back(make_typevar(name));
                        }
                    }
                }
            }
        }
    }

    // Check if all patterns are concrete (no TypeVar).
    bool all_concrete = true;
    for (auto* p : spec_patterns)
        if (p->kind == LogosType::Kind::TypeVar) { all_concrete = false; break; }

    if (all_concrete) {
        // Compute concrete name (e.g. "Pair__i32") and build SemaStructInfo.
        auto* inst_type = make_generic_struct(sname, spec_patterns);
        std::string concrete = concrete_struct_name(inst_type);

        SemaStructInfo info;
        info.package = cur_package_;
        if (node.has_key(la::FIELDS)) {
            auto fields = arr_of(node.get(la::FIELDS.code));
            for (uint64_t i = 0; i < fields.size(); ++i) {
                auto fnode = map_of(fields.get(i));
                auto fname = str_of(fnode.get(la::NAME.code));
                auto ftype = resolve_type(map_of(fnode.get(la::TYPE.code)));
                bool fpub = fnode.has_key(la::IS_PUB) &&
                            fnode.get(la::IS_PUB.code).is_value() &&
                            fnode.get(la::IS_PUB.code).as_value<uint8_t>() != 0;
                info.fields.push_back({fname, ftype, fpub});
            }
        }
        struct_specs_sema_[std::move(concrete)] = std::move(info);
    }

    // Clean up pattern TypeVars.
    for (auto& tp : pattern_tvars)
        current_type_params_.erase(tp.name);
}

void SemaChecker::collect_class(TinyMapView node) {
    auto cname = std::string(str_of(node.get(la::NAME.code)));
    ctx_ = std::format("class {}", cname);

    SemaClassInfo& info = classes_[cname];
    info.package = cur_package_;

    // Read type parameters (generic classes: class Box<T> { ... })
    info.type_params = read_type_params(node);
    push_type_params(info.type_params);

    // Read IS_ABSTRACT flag
    if (node.has_key(la::IS_ABSTRACT)) {
        AnyVal av = node.get(la::IS_ABSTRACT.code);
        info.is_abstract = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }

    // Read parent class
    if (node.has_key(la::PARENT)) {
        auto parent_av = node.get(la::PARENT.code);
        // PARENT is now a simple_type node: TYPE_REF or GENERIC_INST
        auto parent_node = map_of(parent_av);
        info.parent_name = std::string(str_of(parent_node.get(la::NAME.code)));
        // Read parent type args if generic (e.g. extends Container<T>)
        if (code_of(parent_node) == la::GENERIC_INST && parent_node.has_key(la::ITEMS)) {
            auto items = arr_of(parent_node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i)
                info.parent_type_args.push_back(resolve_type(map_of(items.get(i))));
        }
        if (!classes_.count(info.parent_name))
            error(std::format("class '{}': unknown parent '{}'", cname, info.parent_name));
    }

    // Process class members: collect own fields and method signatures
    if (node.has_key(la::ITEMS)) {
        auto members = arr_of(node.get(la::ITEMS.code));
        for (uint64_t i = 0; i < members.size(); ++i) {
            auto m = map_of(members.get(i));
            int32_t mc = code_of(m);

            if (mc == la::FIELD_DEF) {
                auto fname = str_of(m.get(la::NAME.code));
                auto ftype = resolve_type(map_of(m.get(la::TYPE.code)));
                bool fpub = m.has_key(la::IS_PUB) &&
                            m.get(la::IS_PUB.code).is_value() &&
                            m.get(la::IS_PUB.code).as_value<uint8_t>() != 0;
                bool fvar = false;
                if (m.has_key(la::IS_VARIADIC)) {
                    AnyVal av = m.get(la::IS_VARIADIC.code);
                    fvar = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
                }
                // Own fields (all_fields built in finalize_classes)
                info.all_fields.push_back({std::string(fname), ftype, fpub, fvar});

            } else if (mc == la::FN || mc == la::ABSTRACT_FN) {
                collect_fn(m, cname);
            } else if (mc == la::STATIC_FN) {
                // Static methods: collected as free functions with class prefix
                collect_fn(m, cname);
            }
        }
    }

    pop_type_params(classes_[cname].type_params);
}

void SemaChecker::finalize_classes() {
    // We process in order they appear in classes_ — but to correctly inherit,
    // we do a simple recursive helper that resolves each class at most once.
    std::unordered_map<std::string, bool> done;

    std::function<void(const std::string&)> resolve = [&](const std::string& cname) {
        if (done.count(cname)) return;
        done[cname] = true;

        auto it = classes_.find(cname);
        if (it == classes_.end()) return;
        auto& info = it->second;

        // Resolve parent first
        if (!info.parent_name.empty()) {
            resolve(info.parent_name);
            auto pit = classes_.find(info.parent_name);
            if (pit != classes_.end()) {
                // Prepend parent's all_fields to own
                auto own_fields = std::move(info.all_fields);
                info.all_fields = pit->second.all_fields;
                for (auto& f : own_fields) info.all_fields.push_back(f);
                // Start vtable from parent
                info.vtable_order = pit->second.vtable_order;
            }
        }

        // Add own methods to vtable (skip if already present = override)
        for (auto& [fname, finfo] : funcs_) {
            // Check if this method belongs to cname
            std::string prefix = cname + "__";
            if (fname.rfind(prefix, 0) != 0) continue;
            auto mangled = fname;
            auto vit = std::find(info.vtable_order.begin(), info.vtable_order.end(), mangled);
            if (vit == info.vtable_order.end())
                info.vtable_order.push_back(mangled);
        }
    };

    for (auto& [cname, _] : classes_)
        resolve(cname);
}

void SemaChecker::collect_struct(TinyMapView node) {
    // Struct specialisations don't go into structs_ — they're lowered directly.
    if (is_specialization_struct(node)) return;
    auto sname = std::string(str_of(node.get(la::NAME.code)));
    ctx_ = std::format("struct {}", sname);
    SemaStructInfo info;
    info.type_params = read_type_params(node);
    info.package = cur_package_;
    push_type_params(info.type_params);
    if (node.has_key(la::FIELDS)) {
        auto fields = arr_of(node.get(la::FIELDS.code));
        for (uint64_t i = 0; i < fields.size(); ++i) {
            auto fnode = map_of(fields.get(i));
            auto fname = str_of(fnode.get(la::NAME.code));
            auto ftype = resolve_type(map_of(fnode.get(la::TYPE.code)));
            bool fpub = fnode.has_key(la::IS_PUB) &&
                        fnode.get(la::IS_PUB.code).is_value() &&
                        fnode.get(la::IS_PUB.code).as_value<uint8_t>() != 0;
            bool fvar = false;
            if (fnode.has_key(la::IS_VARIADIC)) {
                AnyVal av = fnode.get(la::IS_VARIADIC.code);
                fvar = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
            }
            info.fields.push_back({fname, ftype, fpub, fvar});
        }
    }
    structs_[sname] = std::move(info);
    // Methods must be collected with the struct's type params in scope.
    if (node.has_key(la::ITEMS)) {
        auto methods = arr_of(node.get(la::ITEMS.code));
        for (uint64_t m = 0; m < methods.size(); ++m) {
            auto method = map_of(methods.get(m));
            int32_t mc = code_of(method);
            if (mc == la::FN || mc == la::STATIC_FN) collect_fn(method, sname);
        }
    }
    pop_type_params(structs_[sname].type_params);
}

const LogosType* SemaChecker::try_resolve_as_known_type(std::string_view name) {
    if (name == "i32")  return prim(LogosType::Kind::I32);
    if (name == "i64")  return prim(LogosType::Kind::I64);
    if (name == "f64")  return prim(LogosType::Kind::F64);
    if (name == "f32")  return prim(LogosType::Kind::F32);
    if (name == "bool") return prim(LogosType::Kind::Bool);
    if (name == "u8")   return prim(LogosType::Kind::U8);
    if (name == "i8")   return prim(LogosType::Kind::I8);
    if (name == "i16")  return prim(LogosType::Kind::I16);
    if (name == "u16")  return prim(LogosType::Kind::U16);
    if (name == "u32")  return prim(LogosType::Kind::U32);
    if (name == "u64")  return prim(LogosType::Kind::U64);
    if (name == "i56")  return prim(LogosType::Kind::I56);
    if (name == "u56")  return prim(LogosType::Kind::U56);
    if (name == "i128") return prim(LogosType::Kind::I128);
    if (name == "u128") return prim(LogosType::Kind::U128);
    if (name == "void") return prim(LogosType::Kind::Void);
    auto ait = type_aliases_.find(std::string(name));
    if (ait != type_aliases_.end()) return ait->second;
    if (structs_.count(std::string(name))) return make_struct_type(name);
    if (enums_.count(std::string(name)))   return make_enum_type(name);
    return nullptr;
}

bool SemaChecker::is_known_type_name(std::string_view name) const {
    static constexpr const char* prims[] = {
        "i32","i64","f64","f32","bool","u8","i8","u32","u64","void",
        "i16","u16","i56","u56","i128","u128",nullptr
    };
    for (int i = 0; prims[i]; ++i) if (prims[i] == name) return true;
    return structs_.count(std::string(name)) ||
           enums_.count(std::string(name))   ||
           type_aliases_.count(std::string(name));
}

// Walk a type AST node, registering any unresolved IDENT as a TypeVar in

void SemaChecker::extract_typevars_from_type_node(TinyMapView node,
                                         std::vector<TypeParam>& out_tvars) {
    int32_t tc = code_of(node);
    if (tc == la::PTR_TYPE || tc == la::REF_TYPE || tc == la::MUT_REF_TYPE) {
        if (node.has_key(la::POINTEE))
            extract_typevars_from_type_node(
                map_of(node.get(la::POINTEE.code)), out_tvars);
    } else if (tc == la::ARR_TYPE) {
        if (node.has_key(la::TYPE))
            extract_typevars_from_type_node(
                map_of(node.get(la::TYPE.code)), out_tvars);
    } else if (tc == la::TYPE_REF) {
        auto name = str_of(node.get(la::NAME.code));
        if (!is_known_type_name(name) &&
            !current_type_params_.count(std::string(name))) {
            current_type_params_[std::string(name)] = make_typevar(name);
            out_tvars.push_back({std::string(name), {}});
        }
    }
}

// Return true when ANY element of the type_param_list is a concrete type

bool SemaChecker::is_specialization_fn(TinyMapView node) {
    if (!node.has_key(la::TYPE_PARAMS)) return false;
    AnyVal tpav = node.get(la::TYPE_PARAMS.code);
    if (tpav.is_null()) return false;
    auto tplist = map_of(tpav);
    if (!tplist.has_key(la::ITEMS)) return false;
    auto items = arr_of(tplist.get(la::ITEMS.code));
    for (uint64_t i = 0; i < items.size(); ++i) {
        auto n = map_of(items.get(i));
        int32_t c = code_of(n);
        if (c == la::PTR_TYPE || c == la::ARR_TYPE)
            return true;  // structured pattern → specialisation
        if (c == la::TYPE_PARAM && !n.has_key(la::ITEMS)) {
            auto name = str_of(n.get(la::NAME.code));
            if (try_resolve_as_known_type(name))
                return true;  // concrete type name → specialisation
        }
    }
    return false;
}


bool SemaChecker::is_specialization_struct(TinyMapView node) {
    return is_specialization_fn(node);  // identical check
}

lir::LStructDef SemaChecker::lower_spec_struct(TinyMapView node) {
    auto sname = std::string(str_of(node.get(la::NAME.code)));
    ctx_ = std::format("struct {} (specialization)", sname);

    lir::LStructDef sd;
    sd.name = sname;
    sd.is_specialization = true;

    // Parse spec type-param list: populate spec_patterns and TypeVar scope.
    std::vector<TypeParam> pattern_tvars;
    if (node.has_key(la::TYPE_PARAMS)) {
        AnyVal tpav = node.get(la::TYPE_PARAMS.code);
        if (!tpav.is_null()) {
            auto tplist = map_of(tpav);
            if (tplist.has_key(la::ITEMS)) {
                auto tpitems = arr_of(tplist.get(la::ITEMS.code));
                for (uint64_t i = 0; i < tpitems.size(); ++i) {
                    auto tpnode = map_of(tpitems.get(i));
                    int32_t tc  = code_of(tpnode);
                    if (tc == la::LIFETIME_PARAM) continue;  // skip — deferred to borrow checker
                    if (tc == la::PTR_TYPE || tc == la::ARR_TYPE) {
                        extract_typevars_from_type_node(tpnode, pattern_tvars);
                        sd.spec_patterns.push_back(resolve_type(tpnode));
                    } else if (tc == la::TYPE_PARAM) {
                        auto name = str_of(tpnode.get(la::NAME.code));
                        if (tpnode.has_key(la::ITEMS)) {
                            // TypeVar with bounds — stays TypeVar in pattern.
                            current_type_params_[std::string(name)] = make_typevar(name);
                            TypeParam tp; tp.name = std::string(name);
                            pattern_tvars.push_back(std::move(tp));
                            sd.spec_patterns.push_back(make_typevar(name));
                        } else {
                            auto* known = try_resolve_as_known_type(name);
                            if (known) {
                                sd.spec_patterns.push_back(known);
                            } else {
                                current_type_params_[std::string(name)] = make_typevar(name);
                                pattern_tvars.push_back({std::string(name), {}});
                                sd.spec_patterns.push_back(make_typevar(name));
                            }
                        }
                    }
                }
            }
        }
    }

    // Lower fields (TypeVars from patterns now in scope).
    if (node.has_key(la::FIELDS)) {
        auto fields = arr_of(node.get(la::FIELDS.code));
        for (uint64_t i = 0; i < fields.size(); ++i) {
            auto fnode = map_of(fields.get(i));
            auto fname = str_of(fnode.get(la::NAME.code));
            auto ftype = resolve_type(map_of(fnode.get(la::TYPE.code)));
            sd.fields.push_back({std::string(fname), ftype});
        }
    }

    // Lower methods.
    if (node.has_key(la::ITEMS)) {
        auto methods = arr_of(node.get(la::ITEMS.code));
        for (uint64_t m = 0; m < methods.size(); ++m) {
            auto method = map_of(methods.get(m));
            if (code_of(method) == la::FN)
                sd.methods.push_back(lower_fn(method, sname));
        }
    }

    // Clean up pattern TypeVars.
    for (auto& tp : pattern_tvars)
        current_type_params_.erase(tp.name);

    return sd;
}

// ── lower_spec_fn ─────────────────────────────────────────────
// Like lower_fn but for specialisation definitions.
// Populates spec_patterns and routes the result to prog.specialisations.

lir::LFunction SemaChecker::lower_spec_fn(TinyMapView node) {
    auto raw_name = str_of(node.get(la::NAME.code));
    ctx_ = std::format("fn {} (specialization)", raw_name);
    node_line_ = get_line(node);

    lir::LFunction fn;
    fn.name = std::string(raw_name);
    fn.is_specialization = true;

    // Parse spec type-param list: populate fn.spec_patterns and scope TypeVars.
    std::vector<TypeParam> pattern_tvars;
    if (node.has_key(la::TYPE_PARAMS)) {
        AnyVal tpav = node.get(la::TYPE_PARAMS.code);
        if (!tpav.is_null()) {
            auto tplist = map_of(tpav);
            if (tplist.has_key(la::ITEMS)) {
                auto tpitems = arr_of(tplist.get(la::ITEMS.code));
                for (uint64_t i = 0; i < tpitems.size(); ++i) {
                    auto tpnode = map_of(tpitems.get(i));
                    int32_t tc  = code_of(tpnode);

                    if (tc == la::LIFETIME_PARAM) continue;  // skip — deferred to borrow checker
                    if (tc == la::PTR_TYPE || tc == la::ARR_TYPE) {
                        // Structured pattern: extract TypeVars then resolve.
                        extract_typevars_from_type_node(tpnode, pattern_tvars);
                        fn.spec_patterns.push_back(resolve_type(tpnode));

                    } else if (tc == la::TYPE_PARAM) {
                        auto name = str_of(tpnode.get(la::NAME.code));
                        if (tpnode.has_key(la::ITEMS)) {
                            // TypeVar with bounds — still a TypeVar in patterns.
                            current_type_params_[std::string(name)] =
                                make_typevar(name);
                            TypeParam tp; tp.name = std::string(name);
                            // Read bounds for completeness.
                            auto bounds = arr_of(tpnode.get(la::ITEMS.code));
                            for (uint64_t b = 0; b < bounds.size(); ++b) {
                                auto bn = map_of(bounds.get(b));
                                if (code_of(bn) == la::TRAIT_BOUND)
                                    tp.bounds.push_back(
                                        {std::string(str_of(bn.get(la::NAME.code)))});
                            }
                            pattern_tvars.push_back(std::move(tp));
                            fn.spec_patterns.push_back(make_typevar(name));
                        } else {
                            // Plain IDENT: known type → concrete; else → TypeVar.
                            auto* known = try_resolve_as_known_type(name);
                            if (known) {
                                fn.spec_patterns.push_back(known);
                            } else {
                                current_type_params_[std::string(name)] =
                                    make_typevar(name);
                                pattern_tvars.push_back({std::string(name), {}});
                                fn.spec_patterns.push_back(make_typevar(name));
                            }
                        }
                    }
                }
            }
        }
    }

    // Resolve params and return type (TypeVars from patterns are now in scope).
    fn.ret_type = node.has_key(la::RET_TYPE)
        ? resolve_type(map_of(node.get(la::RET_TYPE.code)))
        : void_t();
    ret_type_ = fn.ret_type;

    scope_.clear();
    push_scope();

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
                    const LogosType* ptype;
                    if (p.has_key(la::IS_REF)) {
                        auto sit = current_type_params_.find("Self");
                        auto* self_t = sit != current_type_params_.end() ? sit->second : error_t();
                        bool is_mut = !p.get(la::IS_MUT.code).is_null();
                        ptype = make_ref(is_mut, self_t);
                    } else {
                        ptype = resolve_type(map_of(p.get(la::TYPE.code)));
                    }
                    define(pname, ptype);
                    fn.params.push_back({std::string(pname), ptype});
                }
            }
        }
    }

    if (!fn.is_extern && node.has_key(la::BODY)) {
        auto body_node = map_of(node.get(la::BODY.code));
        fn.body = lower_block(body_node);
        if (fn.ret_type && fn.ret_type->kind != LogosType::Kind::Void &&
            fn.ret_type->kind != LogosType::Kind::Error &&
            !block_always_returns(body_node)) {
            error("not all paths return a value");
        }
    }

    pop_scope();

    // Remove pattern TypeVars from scope.
    for (auto& tp : pattern_tvars)
        current_type_params_.erase(tp.name);

    return fn;
}

void SemaChecker::collect_fn(TinyMapView node, std::string_view struct_ctx) {
    auto raw_name = str_of(node.get(la::NAME.code));
    std::string mangled = struct_ctx.empty()
        ? std::string(raw_name)
        : std::string(struct_ctx) + "__" + std::string(raw_name);
    ctx_ = std::format("fn {}", mangled);

    // Specialisations are validated and lowered inline by lower_spec_fn;
    // skip collection-phase registration entirely.
    if (is_specialization_fn(node)) return;

    SemaFuncInfo info;
    info.type_params = read_type_params(node);
    // Allow overloading: a non-generic base case and a generic version
    // can coexist with the same name (for variadic recursion base cases).
    if (funcs_.count(mangled)) {
        bool new_is_generic = !info.type_params.empty();
        bool old_is_generic = !funcs_[mangled].type_params.empty();
        if (new_is_generic == old_is_generic) {
            error(std::format("duplicate function '{}'", mangled));
            return;
        }
        // Store the generic version separately; non-generic stays in funcs_.
        if (new_is_generic) {
            // Continue with collection, but store under generic_funcs_
            push_type_params(info.type_params);
            if (node.has_key(la::PARAMS)) {
                auto params_av = node.get(la::PARAMS.code);
                if (params_av.is_pointer()) {
                    auto params_node = map_of(params_av);
                    if (params_node.has_key(la::ITEMS)) {
                        auto arr = arr_of(params_node.get(la::ITEMS.code));
                        for (uint64_t i = 0; i < arr.size(); ++i) {
                            auto p = map_of(arr.get(i));
                            if (code_of(p) != la::PARAM) continue;
                            const LogosType* pt;
                            if (p.has_key(la::IS_REF)) {
                                auto sit = current_type_params_.find("Self");
                                auto* self_t = sit != current_type_params_.end() ? sit->second : error_t();
                                bool is_mut = !p.get(la::IS_MUT.code).is_null();
                                pt = make_ref(is_mut, self_t);
                            } else {
                                pt = p.has_key(la::TYPE)
                                    ? resolve_type(map_of(p.get(la::TYPE.code))) : error_t();
                            }
                            info.param_types.push_back(pt);
                        }
                    }
                }
            }
            info.ret_type = node.has_key(la::RET_TYPE)
                ? resolve_type(map_of(node.get(la::RET_TYPE.code))) : void_t();
            if (node.has_key(la::IS_PUB)) {
                AnyVal av = node.get(la::IS_PUB.code);
                info.is_pub = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
            }
            if (node.has_key(la::IS_UNSAFE)) {
                AnyVal av = node.get(la::IS_UNSAFE.code);
                info.is_unsafe = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
            }
            if (code_of(node) == la::EXTERN_FN) {
                info.is_pub = true;
                info.is_unsafe = true;
            }
            if (node.has_key(la::IS_CONST)) {
                AnyVal av = node.get(la::IS_CONST.code);
                info.is_const = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
            }
            pop_type_params(info.type_params);
            if (!impl_type_params_.empty()) {
                auto combined = impl_type_params_;
                combined.insert(combined.end(), info.type_params.begin(), info.type_params.end());
                info.type_params = std::move(combined);
            }
            generic_funcs_[mangled] = std::move(info);
            return;
        }
        // else: new is non-generic, old is generic — move old to generic_funcs_
        generic_funcs_[mangled] = std::move(funcs_[mangled]);
        funcs_.erase(mangled);
    }
    push_type_params(info.type_params);
    if (node.has_key(la::PARAMS)) {
        auto params_av = node.get(la::PARAMS.code);
        if (params_av.is_pointer()) {
            auto params_node = map_of(params_av);
            if (params_node.has_key(la::ITEMS)) {
                auto arr = arr_of(params_node.get(la::ITEMS.code));
                for (uint64_t i = 0; i < arr.size(); ++i) {
                    auto p = map_of(arr.get(i));
                    if (code_of(p) != la::PARAM) continue;
                    const LogosType* pt;
                    if (p.has_key(la::IS_REF)) {
                        auto sit = current_type_params_.find("Self");
                        auto* self_t = sit != current_type_params_.end() ? sit->second : error_t();
                        bool is_mut = !p.get(la::IS_MUT.code).is_null();
                        pt = make_ref(is_mut, self_t);
                    } else {
                        pt = resolve_type(map_of(p.get(la::TYPE.code)));
                    }
                    info.param_types.push_back(pt);
                }
            }
        }
    }
    info.ret_type = node.has_key(la::RET_TYPE)
        ? resolve_type(map_of(node.get(la::RET_TYPE.code)))
        : void_t();
    // Vararg flag (for extern fn with ... params)
    if (node.has_key(la::IS_VARARG)) {
        AnyVal av = node.get(la::IS_VARARG.code);
        info.is_vararg = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }
    // Visibility: pub fn → is_pub = true; extern fn is always pub (C FFI).
    // extern fn is also always unsafe (FFI calls require unsafe context, like Rust).
    if (code_of(node) == la::EXTERN_FN) {
        info.is_pub = true;
        info.is_unsafe = true;
    } else if (node.has_key(la::IS_PUB)) {
        AnyVal av = node.get(la::IS_PUB.code);
        info.is_pub = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }
    // const fn marker
    if (node.has_key(la::IS_CONST)) {
        AnyVal av = node.get(la::IS_CONST.code);
        info.is_const = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }
    // unsafe fn marker
    if (node.has_key(la::IS_UNSAFE)) {
        AnyVal av = node.get(la::IS_UNSAFE.code);
        info.is_unsafe = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }
    info.source_file = file_;
    info.package = cur_package_;
    pop_type_params(info.type_params);
    // Prepend impl-level type params AFTER the push/pop cycle so they remain
    // in current_type_params_ (managed by collect_impl's push/pop).
    if (!impl_type_params_.empty()) {
        auto combined = impl_type_params_;
        combined.insert(combined.end(), info.type_params.begin(), info.type_params.end());
        info.type_params = std::move(combined);
    }
    // Store body AST for const fn evaluation.
    bool became_const = info.is_const;
    if (became_const && node.has_key(la::BODY)) {
        ConstFnBody cfb;
        cfb.body = map_of(node.get(la::BODY.code));
        // Read parameter names.
        if (node.has_key(la::PARAMS)) {
            auto params_av = node.get(la::PARAMS.code);
            if (params_av.is_pointer()) {
                auto params_node = map_of(params_av);
                if (params_node.has_key(la::ITEMS)) {
                    auto arr = arr_of(params_node.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < arr.size(); ++i) {
                        auto p = map_of(arr.get(i));
                        if (code_of(p) != la::PARAM) continue;
                        cfb.param_names.push_back(std::string(str_of(p.get(la::NAME.code))));
                    }
                }
            }
        }
        const_fn_bodies_[mangled] = std::move(cfb);
    }
    funcs_[mangled] = std::move(info);  // must come after body storage (info.is_const read above)
}


} // namespace logos::compiler
