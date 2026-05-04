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
        // Scalar kinds (u64/i32/bool/...) — concrete_base is the type's
        // canonical name. Lets bare scalars resolve assoc types via the
        // Primitive→Container blanket chain in stdlib.
        bool scalar_base = false;
        switch (sbv.kind()) {
            case LogosType::Kind::Bool:
            case LogosType::Kind::I8:  case LogosType::Kind::I16:
            case LogosType::Kind::I32: case LogosType::Kind::I64:
            case LogosType::Kind::U8:  case LogosType::Kind::U16:
            case LogosType::Kind::U32: case LogosType::Kind::U64:
            case LogosType::Kind::F32: case LogosType::Kind::F64:
                scalar_base = true; break;
            default: break;
        }
        if (sbv.kind() == LogosType::Kind::Struct ||
            sbv.kind() == LogosType::Kind::ZonedStruct ||
            sbv.kind() == LogosType::Kind::Enum ||
            scalar_base) {
            std::string concrete_base;
            if (sbv.kind() == LogosType::Kind::Struct ||
                sbv.kind() == LogosType::Kind::ZonedStruct)
                concrete_base = concrete_struct_name(subbed_base);
            else if (sbv.kind() == LogosType::Kind::Enum)
                concrete_base = std::string(sbv.enum_name());
            else
                concrete_base = type_str(subbed_base);

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
                StrSet seen_pri;
                if (!bi.bound_trait.empty() &&
                    !mono_has_impl_recursive(bi.bound_trait, concrete_base, seen_pri)) continue;
                bool all_extra = true;
                for (auto& eb : bi.extra_bounds) {
                    StrSet seen_eb;
                    if (!mono_has_impl_recursive(eb, concrete_base, seen_eb)) {
                        all_extra = false; break;
                    }
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
    case LogosType::Kind::CfgSlotType: {
        // <type:CFG.path> — extract the type stored at the given path of
        // HermesStatic-bound CFG. CFG can be a const-generic param
        // (resolves through `s`) or a type alias to an HStaticLit (already
        // a concrete bound when type aliases are inlined). When CFG is not
        // yet concrete, stay deferred.
        //
        // The path is encoded in `assoc_type_name` (one entry per step,
        // each as `kind_byte + payload`, joined by 0x1F):
        //   'F' + name   — string-keyed map field
        //   'I' + intstr — integer-keyed map field
        //   'A' + intstr — array index
        std::string cfg_name = std::string(tv.type_var_name());
        std::string path_enc = std::string(tv.assoc_type_name());
        TypeRef cfg = nullptr;
        auto sit = s.find(cfg_name);
        if (sit != s.end()) cfg = sit->second;
        if (cfg) cfg = subst_type(cfg, s);
        if (!cfg || TypeRef(cfg).kind() != LogosType::Kind::HStaticLit) return tv;
        uint64_t hash = (uint64_t)cfg.const_val().value_or(0);
        auto rit = out_.hstatic_registry_.find(hash);
        if (rit == out_.hstatic_registry_.end()) return tv;
        if (!rit->second || rit->second->mirror_offset_ == hermes::arena_offset_t{}) return tv;
        lir_view::ExprRef eref(out_.type_pool.arena(), rit->second->mirror_offset_);
        if (eref.kind() != lir_schema::expr::Code::HermesLit) return tv;
        // Decode path.
        struct Step { char kind; std::string name; int64_t index; };
        std::vector<Step> steps;
        {
            size_t p = 0;
            while (p < path_enc.size()) {
                Step st{};
                st.kind = path_enc[p++];
                size_t e = path_enc.find('\x1F', p);
                if (e == std::string::npos) e = path_enc.size();
                std::string payload = path_enc.substr(p, e - p);
                if (st.kind == 'F') st.name = std::move(payload);
                else st.index = std::stoll(payload);
                steps.push_back(std::move(st));
                p = e + 1;
            }
        }
        // Walk path through the Hermes value.
        lir_view::HermesValRef cur = lir_view::EHermesLitView{eref}.root();
        for (auto& st : steps) {
            using K = lir_schema::hermes_val::Code;
            bool found = false;
            if (st.kind == 'F' || st.kind == 'I') {
                if (cur.kind() != K::Map) return tv;
                auto map = lir_view::HVMapView{cur};
                if (st.kind == 'F' && !map.int_keyed()) {
                    for (uint64_t i = 0, n = map.size(); i < n; ++i)
                        if (map.str_key(i) == st.name) { cur = map.value(i); found = true; break; }
                } else if (st.kind == 'I' && map.int_keyed()) {
                    for (uint64_t i = 0, n = map.size(); i < n; ++i)
                        if (map.int_key(i) == st.index) { cur = map.value(i); found = true; break; }
                }
            } else if (st.kind == 'A') {
                if (cur.kind() != K::Array) return tv;
                auto arr = lir_view::HVArrayView{cur};
                if ((uint64_t)st.index >= arr.size()) return tv;
                cur = arr.elem((uint64_t)st.index);
                found = true;
            }
            if (!found) return tv;
        }
        if (cur.kind() == lir_schema::hermes_val::Code::Type) {
            std::string tname(lir_view::HVTypeView{cur}.name());
            auto alloc_kind = [&](LogosType::Kind k) -> TypeRef {
                LogosTypeBuilder b; b.kind = k;
                return out_.type_pool.alloc(std::move(b));
            };
            if (tname == "u8")   return alloc_kind(LogosType::Kind::U8);
            if (tname == "u16")  return alloc_kind(LogosType::Kind::U16);
            if (tname == "u32")  return alloc_kind(LogosType::Kind::U32);
            if (tname == "u64")  return alloc_kind(LogosType::Kind::U64);
            if (tname == "i8")   return alloc_kind(LogosType::Kind::I8);
            if (tname == "i16")  return alloc_kind(LogosType::Kind::I16);
            if (tname == "i32")  return alloc_kind(LogosType::Kind::I32);
            if (tname == "i64")  return alloc_kind(LogosType::Kind::I64);
            if (tname == "f32")  return alloc_kind(LogosType::Kind::F32);
            if (tname == "f64")  return alloc_kind(LogosType::Kind::F64);
            if (tname == "bool") return alloc_kind(LogosType::Kind::Bool);
            for (auto& sd : out_.structs)
                if (sd.name == tname) {
                    LogosTypeBuilder b;
                    b.kind = sd.is_zoned ? LogosType::Kind::ZonedStruct
                                         : LogosType::Kind::Struct;
                    b.struct_name = tname;
                    return out_.type_pool.alloc(std::move(b));
                }
            for (auto& ed : out_.enums)
                if (ed.name == tname) {
                    LogosTypeBuilder b;
                    b.kind = LogosType::Kind::Enum;
                    b.enum_name = tname;
                    return out_.type_pool.alloc(std::move(b));
                }
            return tv;
        }
        return tv;
    }
    default:
        return tv;
    }
}

} // namespace logos::compiler
