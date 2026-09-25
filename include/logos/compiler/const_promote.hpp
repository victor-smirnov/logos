#pragma once
// ── CONST PROMOTION (task #92) — ONE PREDICATE, TWO CONSUMERS ──────────────
//
// Rust promotes a borrow of a CONSTANT expression to `&'static`: `&0i64`,
// `&[]` and `&[1i64,2i64,3i64]` do not point at frame storage at all, so
// nothing dangles and nothing may be refused. Parity is the default; no
// extension needs a deviation here (Victor, 2026-08-24).
//
// ⚠ WHY THIS LIVES IN A HEADER AND NOT AT EITHER CALL SITE. Promotion is TWO
// facts that must agree exactly:
//   • borrow_check must not refuse the borrow  (prov_of, AddrOfTemp arm)
//   • mlir_gen must not put the referent in the FRAME (gen_expr_kind,
//     EAddrOfTempView) — today that path is `create_entry_alloca` + store,
//     i.e. a stack slot, which is precisely what dangles on return.
// If the two drift APART in the permissive direction the result is a checker
// that admits a real dangle — the defect class this whole arc exists to
// close. So they read the SAME function, and the promotable set is exactly
// the set the emitter can materialise: any shape this says NO to keeps
// today's frame lowering AND today's refusal, which is safe by construction.
//
// SCOPE, STATED: scalar and `str` literals, and array / tuple / struct literals whose
// every element is itself promotable, nested to any depth (the empty array
// included) — `&[[1, 2], [3, 4]]`, `&(1i64, 2.0)`, `&S { n: 1, a: [0; 2] }`.
// A struct is promoted only when its type has NO DESTRUCTOR (asked of the
// caller: `has_drop`) and is not `UnsafeCell` (interior mutability: a promoted
// `&Cell` would write into read-only storage) — Rust's conditions.
//
// ⚠ THE BORROW CHECKER'S BIR ONCE KEPT A PRIVATE COPY of this predicate that
// took nested arrays and `&"s"`: `fn f() -> &'static [[i64; 2]; 2] { &[[1, 2],
// [3, 4]] }` was admitted and returned a FRAME address (measured: the caller
// read another frame's bytes). A third consumer is a third copy; there is one.
#include <logos/compiler/lir_view.hpp>

namespace logos::compiler::const_promote {

// A literal with no storage of its own.
//
// An integer literal wider than 64 bits is promotable: the emitter reads BOTH
// halves (ELitIntView::value / value_hi). It was excluded while the emitter
// re-derived the high half by sign extension (wrong code: `*pr != bv` for
// V = 2^64); the full-width read closed that (2026-09-25).
inline bool is_const_scalar(lir_view::ExprRef e,
                            const TypePoolImpl* pool) noexcept {
    using EC = lir_schema::expr::Code;
    if (!e) return false;
    switch (e.kind()) {
        case EC::LitInt:
        case EC::LitFloat:
        case EC::LitBool:
            return true;
        default:
            return false;
    }
}

inline bool is_unsafe_cell(TypeRef t) noexcept {
    if (!t || (t.kind() != LogosType::Kind::Struct && t.kind() != LogosType::Kind::ZonedStruct))
        return false;
    std::string_view n = t.struct_name();
    n = n.substr(0, n.find('$'));   // a mono instance: `UnsafeCell$G1$i64`
    return t.pkg_name() == "logos.lang.cell" && n == "UnsafeCell";
}

// A value that can be materialised whole in read-only static storage.
// `has_drop(TypeRef)` answers whether a STRUCT type has a destructor (its own
// or a field's); each consumer asks its own drop facts.
template <class HasDrop>
bool is_const_value(lir_view::ExprRef e, const TypePoolImpl* pool,
                    const HasDrop& has_drop, int depth = 0) noexcept {
    using EC = lir_schema::expr::Code;
    if (!e || depth > 16) return false;
    if (is_const_scalar(e, pool)) return true;
    bool all = true;
    auto each = [&](lir_view::ExprRef el) {
        if (all && !is_const_value(el, pool, has_drop, depth + 1)) all = false;
    };
    switch (e.kind()) {
        case EC::LitStr:     // a `&str` value: `{ptr, len}` over its own global
            return true;
        case EC::ArrLit:     // an EMPTY array literal answers YES — `&[]`
            lir_view::EArrLitView{e}.each_elem(each);
            return all;
        case EC::TupleLit:
            if (lir_view::ETupleLitView{e}.count() == 0) return false;
            lir_view::ETupleLitView{e}.each_elem(each);
            return all;
        case EC::StructLit: {
            TypeRef t = e.type(pool);
            if (!t || t.kind() != LogosType::Kind::Struct || is_unsafe_cell(t) || has_drop(t))
                return false;
            bool any = false;
            lir_view::EStructLitView{e}.each_field([&](std::string_view, lir_view::ExprRef v) {
                any = true;
                each(v);
            });
            return any && all;
        }
        default:
            return false;
    }
}

// Is `e` the whole borrow of a promotable constant? `&mut` is excluded: it
// needs unique WRITABLE storage, and read-only static storage is neither.
template <class HasDrop>
bool is_promoted_borrow(lir_view::ExprRef e, const TypePoolImpl* pool,
                        const HasDrop& has_drop) noexcept {
    using EC = lir_schema::expr::Code;
    if (!e || e.kind() != EC::AddrOfTemp) return false;
    lir_view::EAddrOfTempView v{e};
    if (v.is_mut()) return false;
    return is_const_value(v.inner(), pool, has_drop);
}

}  // namespace logos::compiler::const_promote
