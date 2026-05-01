// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// mono_subst.cpp — Type substitution for the monomorphization pass.

#include "mono_impl.hpp"

namespace logos::compiler {

TypeRef Mono::subst_type(TypeRef tv, const SubstMap& s) noexcept {
    if (!tv) return tv;
    if (tv.kind() == LogosType::Kind::TypeVar || tv.kind() == LogosType::Kind::ConstVar) {
        auto it = s.find(std::string(tv.type_var_name()));
        if (it != s.end()) return it->second;
        return tv;
    }
    if (tv.kind() == LogosType::Kind::Array) {
        auto elem = subst_type(tv.elem(), s);
        uint64_t size = tv.arr_size();
        std::string symbolic = std::string(tv.arr_size_var());
        if (!symbolic.empty()) {
            // [T; sizeof...(P)] sema lowers to arr_size_var "__sizeof_pack:P".
            // At mono, `cur_packs_` holds the concrete expansion of P — emit
            // its length as the literal size.
            constexpr std::string_view PFX = "__sizeof_pack:";
            if (symbolic.compare(0, PFX.size(), PFX) == 0) {
                std::string pname = symbolic.substr(PFX.size());
                auto pit = cur_packs_.find(pname);
                if (pit != cur_packs_.end()) {
                    size = (uint64_t)pit->second.size();
                    symbolic = "";
                }
            } else {
                auto it = s.find(symbolic);
                if (it != s.end()) {
                    TypeRef itv{it->second};
                    if (itv.const_val()) {
                        size = (uint64_t)*itv.const_val();
                        symbolic = ""; // Resolved to literal
                    } else if (itv.kind() == LogosType::Kind::ConstVar) {
                        symbolic = std::string(itv.type_var_name()); // Still symbolic
                    }
                }
            }
        }
        if (elem == tv.elem() && size == tv.arr_size() && symbolic == tv.arr_size_var()) return tv;
        LogosTypeBuilder nt = tv.to_builder();
        nt.elem = elem;
        nt.arr_size = size;
        nt.arr_size_var = symbolic;
        return out_.type_pool.alloc(nt);
    }
    switch (tv.kind()) {
    case LogosType::Kind::Ptr:
    case LogosType::Kind::Ref:
    case LogosType::Kind::MutRef: {
        auto inner = subst_type(tv.pointee(), s);
        if (inner == tv.pointee()) return tv;
        LogosTypeBuilder nt = tv.to_builder(); nt.pointee = inner;
        return out_.type_pool.alloc(nt);
    }
    case LogosType::Kind::Struct:
    case LogosType::Kind::ZonedStruct: {
        if (tv.type_args().empty()) return tv;
        std::vector<TypeRef> new_args;
        bool changed = false;
        for (auto a : tv.type_args()) {
            auto na = subst_type(a, s);
            changed |= (na != a);
            new_args.push_back(na);
        }
        if (!changed) return tv;
        LogosTypeBuilder nt = tv.to_builder();
        nt.type_args = std::move(new_args);
        // Track this instantiation for struct monomorphization.
        TypeRef result = out_.type_pool.alloc(nt);
        record_needed_struct(result);
        return result;
    }
    case LogosType::Kind::Enum: {
        if (tv.type_args().empty()) return tv;
        std::vector<TypeRef> new_args;
        bool changed = false;
        for (auto a : tv.type_args()) {
            auto na = subst_type(a, s);
            changed |= (na != a);
            new_args.push_back(na);
        }
        if (!changed) {
            // Still record the need even if types didn't change
            // (e.g., non-generic function using Option<i32>).
            record_needed_enum(tv);
            return tv;
        }
        LogosTypeBuilder nt; nt.kind = LogosType::Kind::Enum;
        nt.enum_name = std::string(tv.enum_name());
        nt.type_args = std::move(new_args);
        TypeRef result = out_.type_pool.alloc(std::move(nt));
        record_needed_enum(result);
        return result;
    }
    case LogosType::Kind::Slice: {
        auto elem = subst_type(tv.elem(), s);
        if (elem == tv.elem()) return tv;
        LogosTypeBuilder nt; nt.kind = LogosType::Kind::Slice;
        nt.elem = elem;
        return out_.type_pool.alloc(std::move(nt));
    }
    case LogosType::Kind::Tuple: {
        std::vector<TypeRef> new_elems;
        bool changed = false;
        for (auto e : tv.tuple_elems()) {
            auto ne = subst_type(e, s);
            changed |= (ne != e);
            new_elems.push_back(ne);
        }
        if (!changed) return tv;
        LogosTypeBuilder nt; nt.kind = LogosType::Kind::Tuple;
        nt.tuple_elems = std::move(new_elems);
        return out_.type_pool.alloc(std::move(nt));
    }
    case LogosType::Kind::AssocType: {
        // Resolve: recursively substitute the base, then look up TraitName::ConcreteType::AssocName
        auto subbed_base = subst_type(tv.assoc_base(), s);
        TypeRef sbv{subbed_base};
        if (sbv.kind() == LogosType::Kind::Struct ||
            sbv.kind() == LogosType::Kind::ZonedStruct ||
            sbv.kind() == LogosType::Kind::Enum) {
            std::string concrete_base;
            if (sbv.kind() == LogosType::Kind::Struct ||
                sbv.kind() == LogosType::Kind::ZonedStruct)
                concrete_base = concrete_struct_name(subbed_base);
            else
                concrete_base = std::string(sbv.enum_name());

            std::string key = std::string(tv.trait_name()) + "::" + concrete_base + "::" + std::string(tv.assoc_type_name());
            auto ait = assoc_impls_.find(key);
            if (ait != assoc_impls_.end()) {
                // Collapse nested associated-type chains fully
                return subst_type(ait->second, {});
            }
            // Blanket fallback: when there's an `impl<T: Bound> Trait for T`
            // and `concrete_base` satisfies Bound, use the blanket's assoc.
            for (auto& bi : blanket_impls_) {
                if (bi.trait_name != tv.trait_name()) continue;
                if (!concrete_impls_.count(bi.bound_trait + "::" + concrete_base)) continue;
                bool all_extra = true;
                for (auto& eb : bi.extra_bounds)
                    if (!concrete_impls_.count(eb + "::" + concrete_base)) {
                        all_extra = false; break;
                    }
                if (!all_extra) continue;
                auto bait = bi.assoc_types.find(std::string(tv.assoc_type_name()));
                if (bait == bi.assoc_types.end()) continue;
                SubstMap bsubst;
                bsubst[bi.target_typevar] = subbed_base;
                return subst_type(bait->second, bsubst);
            }
        }
        if (subbed_base != tv.assoc_base()) {
            LogosTypeBuilder nt = tv.to_builder();
            nt.assoc_base = subbed_base;
            return out_.type_pool.alloc(std::move(nt));
        }
        return tv;
    }
    default:
        return tv;
    }
}

} // namespace logos::compiler
