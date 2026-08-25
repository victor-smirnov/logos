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
// SCOPE, STATED: scalar literals and arrays of scalar literals (the empty
// array included). A struct/tuple literal (`&S{n:1}`, `&(1i64,2i64)`) is NOT
// promoted — it stays refused exactly as before, a KNOWN residual divergence
// from Rust, not a new hole.
#include <logos/compiler/lir_view.hpp>

namespace logos::compiler::const_promote {

// A literal with no storage of its own.
//
// ⚠ AN INTEGER LITERAL WIDER THAN 64 BITS IS NOT PROMOTABLE, and the reason is
// that we cannot READ it: `ELitIntView::value()` returns `int64_t`, so
// `gen_promoted_const` re-derived the high half by SIGN EXTENSION and the
// promoted global held a different number from the same literal anywhere else.
// Measured: `let bv: i128 = V; let fr = &bv; let pr = f();` with V = 2^64, 2^65
// and i128::MAX — `*fr == bv` passes and `*pr != bv` fails, boundary exactly at
// 64 bits; 2^64-1 is fine. That is WRONG CODE, not a refusal, so the emitter's
// fail-closed guard never fired: it could build an initializer, just the wrong
// one. Excluding the shape here restores the design's own promise — what this
// predicate says NO to keeps today's frame lowering AND today's refusal — and
// it does so in the SHARED predicate, so the checker and the emitter cannot
// disagree. Lifting it needs a full-width literal accessor first.
inline bool is_const_scalar(lir_view::ExprRef e,
                            const TypePoolImpl* pool) noexcept {
    using EC = lir_schema::expr::Code;
    if (!e) return false;
    switch (e.kind()) {
        case EC::LitInt: {
            TypeRef t = e.type(pool);
            if (t && (t.kind() == LogosType::Kind::I128 ||
                      t.kind() == LogosType::Kind::U128)) return false;
            return true;
        }
        case EC::LitFloat:
        case EC::LitBool:
            return true;
        default:
            return false;
    }
}

// A value that can be materialised whole in read-only static storage.
inline bool is_const_value(lir_view::ExprRef e,
                           const TypePoolImpl* pool) noexcept {
    using EC = lir_schema::expr::Code;
    if (!e) return false;
    if (is_const_scalar(e, pool)) return true;
    if (e.kind() == EC::ArrLit) {
        bool all = true;
        lir_view::EArrLitView{e}.each_elem([&](lir_view::ExprRef el) {
            if (!el || !is_const_scalar(el, pool)) all = false;
        });
        return all;   // an EMPTY array literal answers YES — `&[]`
    }
    return false;
}

// Is `e` the whole borrow of a promotable constant? `&mut` is excluded: it
// needs unique WRITABLE storage, and read-only static storage is neither.
inline bool is_promoted_borrow(lir_view::ExprRef e,
                               const TypePoolImpl* pool) noexcept {
    using EC = lir_schema::expr::Code;
    if (!e || e.kind() != EC::AddrOfTemp) return false;
    lir_view::EAddrOfTempView v{e};
    if (v.is_mut()) return false;
    return is_const_value(v.inner(), pool);
}

}  // namespace logos::compiler::const_promote
