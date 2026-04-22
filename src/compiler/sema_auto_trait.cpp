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
    const LogosType* T,
    std::string_view trait_name,
    std::unordered_set<std::string>& visited)
{
    if (!T) return true;
    if (T->kind == Kind::Error) return true;

    // Cycle guard — prevents infinite recursion on recursive types.
    auto cycle_key = type_str(T) + "::" + std::string(trait_name);
    if (visited.count(cycle_key)) return true;
    visited.insert(cycle_key);

    auto has_explicit = [&](const std::string& name) -> bool {
        return impls_.count(std::string(trait_name) + "::" + name) > 0;
    };

    switch (T->kind) {
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
        std::string tstr = type_str(T);
        return has_explicit(tstr);
    }

    // ── Shared reference &T: Send iff T:Sync; Sync iff T:Sync ──────────────
    case Kind::Ref:
        return is_auto_trait_satisfied(T->pointee, "Sync", visited);

    // ── Mutable reference &mut T: Send iff T:Send; Sync iff T:Sync ─────────
    case Kind::MutRef:
        if (trait_name == "Send")
            return is_auto_trait_satisfied(T->pointee, "Send", visited);
        else
            return is_auto_trait_satisfied(T->pointee, "Sync", visited);

    // ── TypeVar: satisfied if bound list includes the trait ─────────────────
    case Kind::TypeVar: {
        auto it = current_type_bounds_.find(T->type_var_name);
        if (it != current_type_bounds_.end()) {
            for (auto& b : it->second)
                if (b.trait_name == trait_name) return true;
        }
        return false;
    }

    // ── Struct / ZonedStruct: explicit impl OR all fields satisfied ─────────
    case Kind::Struct:
    case Kind::ZonedStruct: {
        std::string base = concrete_struct_name(T);
        if (has_explicit(base) || has_explicit(type_str(T))) return true;
        auto* si = get_struct_si(T);
        if (!si) {
            si = get_datatype_si(T);
            if (!si) return true; // unknown struct — be lenient
        }
        for (auto& f : si->fields) {
            if (!is_auto_trait_satisfied(f.type, trait_name, visited)) {
                if (last_offender_.field_name.empty())
                    last_offender_ = {std::string(f.name), f.type};
                return false;
            }
        }
        return true;
    }

    // ── Enum: explicit impl OR every variant payload satisfied ──────────────
    case Kind::Enum: {
        if (has_explicit(T->enum_name) || has_explicit(type_str(T))) return true;
        auto* ei = get_enum_si(T);
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
        return T->elem ? is_auto_trait_satisfied(T->elem, trait_name, visited) : true;

    // ── Slice &[T]: element must satisfy (slice is Send iff T: Send) ────────
    case Kind::Slice:
        return T->elem ? is_auto_trait_satisfied(T->elem, trait_name, visited) : true;

    // ── Tuple: every element must satisfy ───────────────────────────────────
    case Kind::Tuple:
        for (auto* e : T->tuple_elems)
            if (!is_auto_trait_satisfied(e, trait_name, visited)) return false;
        return true;

    // ── Conservative false for everything else ──────────────────────────────
    default:
        return false;
    }
}

} // namespace logos::compiler
