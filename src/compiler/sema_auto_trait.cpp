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

// #438: the four markers this engine special-cases are LANG ITEMS
// (logos.lang.marker::{Send,Sync,Unpin,Fst}); `impl_trait_id` resolves each to
// the one identity the impl registry is keyed by (and, in a build with no
// stdlib, to a root id the same call produces for the bound's spelling), so a
// user's own `auto trait Send` gets the structural rule and none of the
// reference/pointer rules written for the marker.
bool SemaChecker::is_auto_trait_satisfied(
    TypeRef tv,
    std::string_view trait_name,
    StrSet& visited)
{
    if (!tv) return true;
    if (tv.kind() == Kind::Error) return true;

    const DefId auto_trait_id = impl_trait_id(trait_name);
    const DefId kSend  = impl_trait_id("Send");
    const DefId kSync  = impl_trait_id("Sync");
    const DefId kUnpin = impl_trait_id("Unpin");
    const DefId kFst   = impl_trait_id("Fst");

    // Cycle guard — prevents infinite recursion on recursive types.
    auto cycle_key = type_str(tv) + "::" + std::to_string(auto_trait_id.v);
    if (visited.count(cycle_key)) return true;
    visited.insert(cycle_key);
    auto find_impl = [&](const std::string& name) -> const SemaImplInfo* {
        auto it = impls_.find(ImplKey{auto_trait_id, name});
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
                    const SemaTraitInfo* bti = trait_info(b.trait_def);
                    if (!bti || !bti->is_auto) continue;
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
        return true;

    // Fn items/pointers: satisfied for Send/Sync/Unpin (code identity is
    // process-global), NOT for Fst (a code address in a dumpable block is
    // meaningless in another address space).
    case Kind::FnItem:
    case Kind::FnPtr:
        return auto_trait_id != kFst;

    // ── Unpin: default-TRUE world (Rust semantics) ──────────────────────────
    // Everything is Unpin unless it (transitively) stores a PhantomPinned,
    // is a #[pinned] arena-resident type, or carries an explicit negative
    // impl. Pointers/references are ALWAYS Unpin (the pointee's pin-ness
    // doesn't infect the pointer — Rust's rule). Handled before the
    // Send/Sync-shaped cases below.
    case Kind::Ptr: {
        if (auto_trait_id == kUnpin) {
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
        if (auto_trait_id == kUnpin) return true;   // &T is always Unpin
        if (auto_trait_id == kFst) return false;    // references are never
                                                  // relocation-safe (no opt-in)
        return is_auto_trait_satisfied(tv.pointee(), "Sync", visited);

    // ── Mutable reference &mut T: Send iff T:Send; Sync iff T:Sync ─────────
    case Kind::MutRef:
        if (auto_trait_id == kUnpin) return true;   // &mut T is always Unpin
        if (auto_trait_id == kFst) return false;    // never relocation-safe
        if (auto_trait_id == kSend)
            return is_auto_trait_satisfied(tv.pointee(), "Send", visited);
        else
            return is_auto_trait_satisfied(tv.pointee(), "Sync", visited);

    // ── TypeVar: satisfied if bound list includes the trait ─────────────────
    case Kind::TypeVar: {
        // KEY-IDENTITY: a TYPE-PARAMETER name, scoped to the signature being
        // checked — see SemaChecker::normalize_assoc_eq for the full ground.
        auto it = current_type_bounds_.find(std::string(tv.type_var_name()));
        if (it != current_type_bounds_.end()) {
            for (auto& b : it->second) {
                // #438: the bound's own identity, captured where it was
                // written — a spelling match would accept a homonym's bound.
                DefId bid = b.trait_def ? b.trait_def : impl_trait_id(b.trait_name);
                if (bid == auto_trait_id) return true;
            }
        }
        return false;
    }

    // ── Struct / ZonedStruct: explicit impl OR all fields satisfied ─────────
    case Kind::Struct:
    case Kind::ZonedStruct: {
        // Fst is "pure bytes": a type with a destructor (its own impl Drop —
        // fields recurse below and catch their own) is NOT relocation-safe:
        // bitwise duplication into a dumpable block would double its drop
        // obligation. Explicit impls (positive/negative) still win below.
        if (auto_trait_id == kFst && !drop_fn_for(tv).empty()) {
            int expl0 = check_impl_for_struct(tv);
            if (expl0 == 1) return true;
            return false;
        }
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
            if (auto_trait_id == kSync) return false;
            // Send: defer to the wrapped T (the single field `value: T`).
            if (!tv.type_args().empty())
                return is_auto_trait_satisfied(tv.type_args()[0], "Send", visited);
            return false;
        }
        // Unpin structural opt-outs: PhantomPinned is the canonical !Unpin
        // marker; #[pinned] arena residents have no value form, so pin-ness
        // is moot for them — treat as !Unpin for parity with their intent.
        if (auto_trait_id == kUnpin) {
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
        if (auto_trait_id == kUnpin && si->pinned) return false;   // #[pinned] => !Unpin
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
    case Kind::UnsizedSlice:
        // Bare `[E]` (the VALUE, not the fat &[E] carrier): its bytes are the
        // elements' bytes — Fst iff E is Fst. Other auto traits keep the
        // conservative default (fall through to the bottom).
        if (auto_trait_id == kFst)
            return tv.elem() ? is_auto_trait_satisfied(tv.elem(), "Fst", visited) : true;
        return false;

    case Kind::Enum: {
        if (auto_trait_id == kFst && !drop_fn_for(tv).empty()) return false;
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
        if (auto_trait_id == kFst) return false;    // a fat reference — never
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
        // ADR 0029 S2: ASK THE TYPE. The env is IN the closure type now, so
        // this answers about THIS literal and no other.
        //
        // ⚠ THE NOTE THAT USED TO STAND HERE WAS WRONG IN BOTH DIRECTIONS, and
        // both were measured before the change. It claimed the signature key
        // was "the intent here" and that a union "cannot admit an unsound
        // answer — only a stricter one":
        //
        //   OVER-REFUSAL. Two literals of one signature, one capturing a
        //   `*mut i32` and one capturing an `i32`, made the SECOND one !Send.
        //   Deleting the first admitted the same program — a legal program
        //   refused by a verdict belonging to its sibling, exactly #90's shape
        //   in this map rather than a different one.
        //
        //   AND AN UNSOUND ADMIT, which the note's "safe direction" argument
        //   misses because the union's members are not only literals ASKED
        //   about: a bare `Box<dyn Fn() -> i32>` — a box that may hold any
        //   closure at all, including one built in another package around a
        //   raw pointer — read its Send answer off whatever same-signature
        //   literal the file happened to contain. `fn take(b: Box<dyn Fn() ->
        //   i32>) { need_send(b) }` was ADMITTED when a trivial `move || x`
        //   appeared earlier in the file and REFUSED when it did not, and
        //   refused again when `take` was merely moved above it. A
        //   thread-safety verdict that turns on declaration order is not a
        //   stricter answer, it is no answer.
        //
        // An erased form records no env and stays conservative `false`, which
        // is Rust's `dyn Fn()` without an explicit `+ Send`. A capture-FREE
        // literal is Send, and it is the literal id, not the empty list, that
        // separates the two.
        if (!tv.closure_literal_id()) return false;
        for (auto e : tv.closure_captures())
            if (!is_auto_trait_satisfied(e, trait_name, visited)) return false;
        return true;
    }

    // ── Conservative false for everything else ──────────────────────────────
    default:
        return false;
    }
}

} // namespace logos::compiler
