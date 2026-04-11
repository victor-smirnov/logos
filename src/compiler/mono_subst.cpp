// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// mono_subst.cpp — Type substitution for the monomorphization pass.

#include "mono_impl.hpp"

namespace logos::compiler {

const LogosType* Mono::subst_type(const LogosType* t, const SubstMap& s) noexcept {
    if (!t) return t;
    if (t->kind == LogosType::Kind::TypeVar || t->kind == LogosType::Kind::ConstVar) {
        auto it = s.find(t->type_var_name);
        if (it != s.end()) return it->second;
        return t;
    }
    if (t->kind == LogosType::Kind::Array) {
        auto* elem = subst_type(t->elem, s);
        uint64_t size = t->arr_size;
        std::string symbolic = t->arr_size_var;
        if (!symbolic.empty()) {
            auto it = s.find(symbolic);
            if (it != s.end()) {
                if (it->second->const_val) {
                    size = (uint64_t)*it->second->const_val;
                    symbolic = ""; // Resolved to literal
                } else if (it->second->kind == LogosType::Kind::ConstVar) {
                    symbolic = it->second->type_var_name; // Still symbolic
                }
            }
        }
        if (elem == t->elem && size == t->arr_size && symbolic == t->arr_size_var) return t;
        LogosType nt = *t;
        nt.elem = elem;
        nt.arr_size = size;
        nt.arr_size_var = symbolic;
        return out_.type_pool.alloc(nt);
    }
    switch (t->kind) {
    case LogosType::Kind::Ptr:
    case LogosType::Kind::Ref:
    case LogosType::Kind::MutRef: {
        auto* inner = subst_type(t->pointee, s);
        if (inner == t->pointee) return t;
        LogosType nt = *t; nt.pointee = inner;
        return out_.type_pool.alloc(nt);
    }
    case LogosType::Kind::Struct:
    case LogosType::Kind::Datatype: {
        if (t->type_args.empty()) return t;
        std::vector<const LogosType*> new_args;
        bool changed = false;
        for (auto* a : t->type_args) {
            auto* na = subst_type(a, s);
            changed |= (na != a);
            new_args.push_back(na);
        }
        if (!changed) return t;
        LogosType nt = *t;
        nt.type_args = std::move(new_args);
        // Track this instantiation for struct monomorphization.
        const LogosType* result = out_.type_pool.alloc(nt);
        record_needed_struct(result);
        return result;
    }
    case LogosType::Kind::Class: {
        if (t->type_args.empty()) return t;
        std::vector<const LogosType*> new_args;
        bool changed = false;
        for (auto* a : t->type_args) {
            auto* na = subst_type(a, s);
            changed |= (na != a);
            new_args.push_back(na);
        }
        if (!changed) return t;
        LogosType nt = *t;
        nt.type_args = std::move(new_args);
        const LogosType* result = out_.type_pool.alloc(nt);
        record_needed_class(result);
        return result;
    }
    case LogosType::Kind::Enum: {
        if (t->type_args.empty()) return t;
        std::vector<const LogosType*> new_args;
        bool changed = false;
        for (auto* a : t->type_args) {
            auto* na = subst_type(a, s);
            changed |= (na != a);
            new_args.push_back(na);
        }
        if (!changed) {
            // Still record the need even if types didn't change
            // (e.g., non-generic function using Option<i32>).
            record_needed_enum(t);
            return t;
        }
        LogosType nt; nt.kind = LogosType::Kind::Enum;
        nt.enum_name = t->enum_name;
        nt.type_args = std::move(new_args);
        const LogosType* result = out_.type_pool.alloc(std::move(nt));
        record_needed_enum(result);
        return result;
    }
    case LogosType::Kind::Slice: {
        auto* elem = subst_type(t->elem, s);
        if (elem == t->elem) return t;
        LogosType nt; nt.kind = LogosType::Kind::Slice;
        nt.elem = elem;
        return out_.type_pool.alloc(std::move(nt));
    }
    case LogosType::Kind::Tuple: {
        std::vector<const LogosType*> new_elems;
        bool changed = false;
        for (auto* e : t->tuple_elems) {
            auto* ne = subst_type(e, s);
            changed |= (ne != e);
            new_elems.push_back(ne);
        }
        if (!changed) return t;
        LogosType nt; nt.kind = LogosType::Kind::Tuple;
        nt.tuple_elems = std::move(new_elems);
        return out_.type_pool.alloc(std::move(nt));
    }
    case LogosType::Kind::AssocType: {
        // Resolve: recursively substitute the base, then look up TraitName::ConcreteType::AssocName
        auto* subbed_base = subst_type(t->assoc_base, s);
        if (subbed_base->kind == LogosType::Kind::Struct || subbed_base->kind == LogosType::Kind::Class || subbed_base->kind == LogosType::Kind::Enum) {
            std::string concrete_base;
            if      (subbed_base->kind == LogosType::Kind::Class)  concrete_base = concrete_class_name(subbed_base);
            else if (subbed_base->kind == LogosType::Kind::Struct) concrete_base = concrete_struct_name(subbed_base);
            else                                                   concrete_base = subbed_base->enum_name;

            std::string key = t->trait_name + "::" + concrete_base + "::" + t->assoc_type_name;
            auto ait = assoc_impls_.find(key);
            if (ait != assoc_impls_.end()) {
                // Collapse nested associated-type chains fully
                return subst_type(ait->second, {});
            }
        }
        if (subbed_base != t->assoc_base) {
            LogosType nt = *t;
            nt.assoc_base = subbed_base;
            return out_.type_pool.alloc(std::move(nt));
        }
        return t;
    }
    default:
        return t;
    }
}

} // namespace logos::compiler
