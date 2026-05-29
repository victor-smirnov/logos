#pragma once
//
// move_classify.hpp — shared "is this a move type?" aggregate-recursion skeleton.
//
// Two phases ask the same structural question — sema (live TypePool + generic
// bounds) and borrow_check (post-mono LProgram + TypeSets) — and had two parallel
// `is_move_type` copies that drifted (P2-12 needed both hand-re-synced to add
// tuple/enum/array). This single-sources the Tuple/Array structural recursion +
// the kind dispatch; each phase supplies callbacks reproducing its EXACT leaf
// semantics, so there is NO behavioural change — only the recursion shape is now
// shared and cannot drift.
//
//   leaf(t)          -> std::optional<bool>: phase-specific overrides taken first
//                       (sema: owning-`Box<dyn>` → move, generic TypeVar → move
//                       unless `T: Copy`). nullopt = fall through to kind dispatch.
//   struct_is_move(t)-> bool: a Struct value (sema: !Copy; borrow_check:
//                       needs_drop && !Copy — its copy set lacks auto-Copy).
//   enum_is_move(t)  -> bool: an Enum value (sema: user-Drop or droppable payload;
//                       borrow_check: !Copy-enum && (Drop-impl || any move payload)).
//
// A value is a move type when consuming it invalidates the source (it owns a
// non-Copy value): a tuple/array is a move type iff an element is.

#include <optional>
#include <logos/compiler/sema.hpp>   // TypeRef, LogosType

namespace logos::compiler::moveclass {

template <class Leaf, class StructF, class EnumF>
bool is_move_type(TypeRef t, Leaf&& leaf, StructF&& struct_is_move, EnumF&& enum_is_move) {
    if (!t) return false;
    if (std::optional<bool> r = leaf(t)) return *r;
    switch (TypeRef(t).kind()) {
    case LogosType::Kind::Tuple:
        for (auto e : TypeRef(t).tuple_elems())
            if (e && is_move_type(e, leaf, struct_is_move, enum_is_move)) return true;
        return false;
    case LogosType::Kind::Array:
        return TypeRef(t).elem() &&
               is_move_type(TypeRef(t).elem(), leaf, struct_is_move, enum_is_move);
    case LogosType::Kind::Enum:   return enum_is_move(t);
    case LogosType::Kind::Struct: return struct_is_move(t);
    default:                      return false;
    }
}

}  // namespace logos::compiler::moveclass
