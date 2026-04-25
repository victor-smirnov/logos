// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Auto trait satisfaction engine.
//
// is_auto_trait_satisfied recursively checks whether a concrete type satisfies
// an auto trait (e.g. "Send" or "Sync") based on its structural composition.
//
// Rules:
//   Scalar types (iN/uN/fN/bool/unit/never/fn-ptr): always satisfied.
//   *mut T, *const T: never satisfied unless an explicit unsafe impl exists.
//   &T:             satisfied iff T: Sync  (for both Send and Sync).
//   &mut T:         Send iff T: Send;  Sync iff T: Sync.
//   Struct/ZonedStruct: explicit unsafe impl  OR  every field satisfies.
//   Enum:           explicit unsafe impl  OR  every variant field satisfies.
//   Array/Slice:    element type must satisfy.
//   Tuple:          every element must satisfy.
//   TypeVar:        satisfied iff bounds contain the trait name.
//   Other (TraitObject, Closure, ...): conservative false.

#include "sema_impl.hpp"

namespace logos::compiler {

using Kind = LogosType::Kind;

bool SemaChecker::is_auto_trait_satisfied(
    TypeRef tv,
    std::string_view trait_name,
    StrSet& visited)
{
    if (!tv) return true;
    if (tv.kind() == Kind::Error) return true;

    // Cycle guard — prevents infinite recursion on recursive types.
    auto cycle_key = type_str(tv) + "::" + std::string(trait_name);
    if (visited.count(cycle_key)) return true;
    visited.insert(cycle_key);

    auto has_explicit = [&](const std::string& name) -> bool {
        return impls_.count(std::string(trait_name) + "::" + name) > 0;
    };

    switch (tv.kind()) {
    // ── Scalars and fn-ptr: always Send + Sync ─────────────────────────────
    case Kind::Void:
    case Kind::Bool:
    case Kind::I8:  case Kind::I16:  case Kind::I32:  case Kind::I64:
    case Kind::U8:  case Kind::U16:  case Kind::U32:  case Kind::U64:
    case Kind::I24: case Kind::U24:  case Kind::I56:  case Kind::U56:
    case Kind::I128: case Kind::U128:
    case Kind::F32: case Kind::F64:
    case Kind::IntLit: case Kind::FloatLit:
    case Kind::FnPtr:
        return true;

    // ── Raw pointer: hardcoded !Send / !Sync unless explicit unsafe impl ────
    case Kind::Ptr: {
        std::string tstr = type_str(tv);
        return has_explicit(tstr);
    }

    // ── Shared reference &T: Send iff T:Sync; Sync iff T:Sync ──────────────
    case Kind::Ref:
        return is_auto_trait_satisfied(tv.pointee(), "Sync", visited);

    // ── Mutable reference &mut T: Send iff T:Send; Sync iff T:Sync ─────────
    case Kind::MutRef:
        if (trait_name == "Send")
            return is_auto_trait_satisfied(tv.pointee(), "Send", visited);
        else
            return is_auto_trait_satisfied(tv.pointee(), "Sync", visited);

    // ── TypeVar: satisfied if bound list includes the trait ─────────────────
    case Kind::TypeVar: {
        auto it = current_type_bounds_.find(std::string(tv.type_var_name()));
        if (it != current_type_bounds_.end()) {
            for (auto& b : it->second)
                if (b.trait_name == trait_name) return true;
        }
        return false;
    }

    // ── Struct / ZonedStruct: explicit impl OR all fields satisfied ─────────
    case Kind::Struct:
    case Kind::ZonedStruct: {
        std::string base = concrete_struct_name(tv);
        if (has_explicit(base) || has_explicit(type_str(tv))) return true;
        auto* si = get_struct_si(tv.raw());
        if (!si) {
            si = get_datatype_si(tv.raw());
            if (!si) return true; // unknown struct — be lenient
        }
        // Bug 3 fix: build substitution map from generic type args so that
        // TypeVar fields in generic struct instantiations (e.g. Vec<i32>
        // has field `data: TypeVar("T")`) are replaced with concrete types.
        StrMap<const LogosType*> subst;
        if (!tv.type_args().empty() && !si->type_params.empty()) {
            size_t n = std::min(tv.type_args().size(), si->type_params.size());
            for (size_t j = 0; j < n; ++j)
                subst[si->type_params[j].name] = tv.type_args()[j];
        }
        for (auto& f : si->fields) {
            const LogosType* ftype = f.type;
            if (ftype && TypeRef(ftype).kind() == Kind::TypeVar && !subst.empty()) {
                auto sit = subst.find(std::string(TypeRef(ftype).type_var_name()));
                if (sit != subst.end()) ftype = sit->second;
            }
            if (!is_auto_trait_satisfied(ftype, trait_name, visited)) {
                if (last_offender_.field_name.empty())
                    last_offender_ = {std::string(f.name), ftype};
                return false;
            }
        }
        return true;
    }

    // ── Enum: explicit impl OR every variant payload satisfied ──────────────
    case Kind::Enum: {
        if (has_explicit(std::string(tv.enum_name())) || has_explicit(type_str(tv))) return true;
        auto* ei = get_enum_si(tv.raw());
        if (!ei) return true; // unknown enum — be lenient
        for (auto& v : ei->variants) {
            for (auto* pt : v.payload_types) {
                if (!is_auto_trait_satisfied(pt, trait_name, visited)) {
                    if (last_offender_.field_name.empty())
                        last_offender_ = {std::string(v.name), pt};
                    return false;
                }
            }
        }
        return true;
    }

    // ── Array: element must satisfy ─────────────────────────────────────────
    case Kind::Array:
        return tv.elem() ? is_auto_trait_satisfied(tv.elem(), trait_name, visited) : true;

    // ── Slice &[T]: like &T, both Send and Sync require the element to be Sync ─
    // Bug 2 fix: &[T] is a shared reference; must check T: Sync, not T: trait_name.
    case Kind::Slice:
        return tv.elem() ? is_auto_trait_satisfied(tv.elem(), "Sync", visited) : true;

    // ── Tuple: every element must satisfy ───────────────────────────────────
    case Kind::Tuple:
        for (auto* e : tv.tuple_elems())
            if (!is_auto_trait_satisfied(e, trait_name, visited)) return false;
        return true;

    // ── Conservative false for everything else ──────────────────────────────
    default:
        return false;
    }
}

} // namespace logos::compiler
