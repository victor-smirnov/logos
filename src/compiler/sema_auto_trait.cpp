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

    auto find_impl = [&](const std::string& name) -> const SemaImplInfo* {
        auto it = impls_.find(std::string(trait_name) + "::" + name);
        return it == impls_.end() ? nullptr : &it->second;
    };
    // Check explicit impl (positive or negative) for one of the candidate keys.
    // Returns: 1 = positive (accept), 0 = no match, -1 = negative (reject).
    auto check_impl_for_struct = [&](TypeRef ty) -> int {
        std::string mangled = (ty.kind() == Kind::Struct || ty.kind() == Kind::ZonedStruct)
                            ? concrete_struct_name(ty) : std::string{};
        std::string base = (ty.kind() == Kind::Struct || ty.kind() == Kind::ZonedStruct)
                         ? std::string(ty.struct_name())
                         : (ty.kind() == Kind::Enum ? std::string(ty.enum_name()) : std::string{});
        const SemaImplInfo* info = nullptr;
        if (!mangled.empty()) info = find_impl(mangled);
        if (!info) info = find_impl(type_str(ty));
        if (!info && !base.empty()) info = find_impl(base);
        if (!info) return 0;
        if (info->is_negative) return -1;
        // Positive impl. If it's a generic-target impl with type params, honour
        // the bounds: each impl type param must satisfy its declared bounds
        // when substituted with the corresponding query type arg.
        if (!info->impl_type_params.empty() && info->target_typeref &&
            !ty.type_args().empty()) {
            // Build subst: pattern TypeVar name → query type arg.
            StrMap<TypeRef> subst;
            auto pattern_args = TypeRef(info->target_typeref).type_args();
            size_t n = std::min(pattern_args.size(), ty.type_args().size());
            for (size_t j = 0; j < n; ++j) {
                if (pattern_args[j] && TypeRef(pattern_args[j]).kind() == Kind::TypeVar)
                    subst[std::string(TypeRef(pattern_args[j]).type_var_name())]
                        = ty.type_args()[j];
            }
            // For each impl type param, check each bound that names an auto trait.
            for (auto& tp : info->impl_type_params) {
                auto sit = subst.find(tp.name);
                if (sit == subst.end()) continue;
                for (auto& b : tp.bounds) {
                    auto tit = traits_.find(b.trait_name);
                    if (tit == traits_.end() || !tit->second.is_auto) continue;
                    if (!is_auto_trait_satisfied(sit->second, b.trait_name, visited)) {
                        if (last_offender_.field_name.empty())
                            last_offender_ = {tp.name, sit->second};
                        return -1;
                    }
                }
            }
        }
        return 1;
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
    case Kind::FnItem:
    case Kind::FnPtr:
        return true;

    // ── Unpin: default-TRUE world (Rust semantics) ──────────────────────────
    // Everything is Unpin unless it (transitively) stores a PhantomPinned,
    // is a #[pinned] arena-resident type, or carries an explicit negative
    // impl. Pointers/references are ALWAYS Unpin (the pointee's pin-ness
    // doesn't infect the pointer — Rust's rule). Handled before the
    // Send/Sync-shaped cases below.
    case Kind::Ptr: {
        if (trait_name == "Unpin") {
            auto* info0 = find_impl(type_str(tv));
            if (info0 && info0->is_negative) return false;
            return true;
        }
        std::string tstr = type_str(tv);
        auto* info = find_impl(tstr);
        if (info) return !info->is_negative;
        return false;
    }

    // ── Shared reference &T: Send iff T:Sync; Sync iff T:Sync ──────────────
    case Kind::Ref:
        if (trait_name == "Unpin") return true;   // &T is always Unpin
        return is_auto_trait_satisfied(tv.pointee(), "Sync", visited);

    // ── Mutable reference &mut T: Send iff T:Send; Sync iff T:Sync ─────────
    case Kind::MutRef:
        if (trait_name == "Unpin") return true;   // &mut T is always Unpin
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
        // logos-core 2.2: `UnsafeCell<T>` is the foundational interior-
        // mutability lang-item. A type reachable through `UnsafeCell` is
        // auto-`!Sync` (Rust's rule: shared `&T` can mutate the interior,
        // so two threads racing on it would race-write). `Send` follows
        // T's Send (the cell can move across threads if T can).
        // Recognised by qualified name `logos.lang.cell.UnsafeCell` to
        // avoid colliding with a user-defined `UnsafeCell` in another
        // package.
        if (tv.struct_name() == "UnsafeCell" &&
            tv.pkg_name() == "logos.lang.cell") {
            if (trait_name == "Sync") return false;
            // Send: defer to the wrapped T (the single field `value: T`).
            if (!tv.type_args().empty())
                return is_auto_trait_satisfied(tv.type_args()[0], "Send", visited);
            return false;
        }
        // Unpin structural opt-outs: PhantomPinned is the canonical !Unpin
        // marker; #[pinned] arena residents have no value form, so pin-ness
        // is moot for them — treat as !Unpin for parity with their intent.
        if (trait_name == "Unpin") {
            if (tv.struct_name() == "PhantomPinned" &&
                tv.pkg_name() == "logos.lang.marker") return false;
        }
        int verdict = check_impl_for_struct(tv);
        if (verdict == 1) return true;
        if (verdict == -1) return false;
        auto* si = get_struct_si(tv);
        if (!si) {
            si = get_datatype_si(tv);
            if (!si) return true; // unknown struct — be lenient
        }
        if (trait_name == "Unpin" && si->pinned) return false;   // #[pinned] => !Unpin
        // Bug 3 fix: build substitution map from generic type args so that
        // TypeVar fields in generic struct instantiations (e.g. Vec<i32>
        // has field `data: TypeVar("T")`) are replaced with concrete types.
        StrMap<TypeRef> subst;
        if (!tv.type_args().empty() && !si->type_params.empty()) {
            size_t n = std::min(tv.type_args().size(), si->type_params.size());
            for (size_t j = 0; j < n; ++j)
                subst[si->type_params[j].name] = tv.type_args()[j];
        }
        for (auto& f : si->fields) {
            TypeRef ftype = f.type;
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
        int verdict = check_impl_for_struct(tv);
        if (verdict == 1) return true;
        if (verdict == -1) return false;
        auto* ei = get_enum_si(tv);
        if (!ei) return true; // unknown enum — be lenient
        for (auto& v : ei->variants) {
            for (auto pt : v.payload_types) {
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
        for (auto e : tv.tuple_elems())
            if (!is_auto_trait_satisfied(e, trait_name, visited)) return false;
        return true;

    // ── Closure: walk CAPTURE types (spec lang-types.auto-traits.closure)
    //    so a closure capturing only Send/Sync values auto-derives
    //    Send/Sync. closure_params on the type are the PARAMETER types
    //    (FnPtr-style envelope) — walking those was unsound (T1-7: a
    //    closure capturing `*mut i32` passed `T: Send`). Captures are
    //    recorded per interned closure type at lowering
    //    (closure_capture_env_, union across same-signature literals —
    //    conservative-correct; by-ref captures stored as `&[mut] T` so the
    //    reference rules apply). A closure type with NO recorded literal
    //    (e.g. a bare `dyn Fn` annotation) is conservative `false` — like
    //    Rust's `dyn Fn()` without an explicit `+ Send`.
    case Kind::Closure: {
        auto it = closure_capture_env_.find(type_str(tv));
        if (it == closure_capture_env_.end()) return false;
        for (auto e : it->second)
            if (!is_auto_trait_satisfied(e, trait_name, visited)) return false;
        return true;
    }

    // ── Conservative false for everything else ──────────────────────────────
    default:
        return false;
    }
}

} // namespace logos::compiler
