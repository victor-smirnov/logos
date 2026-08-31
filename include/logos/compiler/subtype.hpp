#pragma once
// B64: variance + outlives subtype relation.
//
// `subtype(sub, sup, outlives_adj, def_variances)` decides whether `sub` is a
// subtype of `sup` — i.e. a value of type `sub` may be used in a context
// expecting `sup`. Goes beyond TypeUID equality by inspecting lifetime
// structure with per-position variance:
//   - Ref(lt, T):  Co in lt, Co in T  →  &'long T  <:  &'short T  when 'long: 'short
//   - MutRef:      Co in lt, INV in T →  &mut &'static T  NOT  <:  &mut &'a T
//   - FnPtr(P..→R) Contra in P, Co in R
//   - Tuple/Slice/Array: Co per element
//   - Struct/Enum<P..>: per-param variance from a user-supplied table; if the
//     def isn't in the table, fall back to invariant (safe default).
//
// Lifetime fragment storage is string-keyed; the outlives query is built from
// the parsed (long, short) pairs (see include/logos/compiler/outlives.hpp).

#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "logos/compiler/outlives.hpp"
#include "logos/compiler/variance.hpp"
#include "logos/compiler/sema.hpp"  // LogosType, TypeRef, types_equal

namespace logos::compiler {

using OutlivesAdj =
    std::unordered_map<std::string, std::unordered_set<std::string>>;

// Forward decl — definition below.
// `permissive_empty` is forwarded to outlives() queries: true at call-site
// coercions where the caller's region inference fills in unresolved
// lifetimes; false at fn-body coercions (return, let with annotation)
// where the lifetimes are fn-scope-fixed.
bool subtype(TypeRef sub, TypeRef sup,
             const OutlivesAdj& adj,
             const DefVarianceTable& vars,
             int depth = 0,
             bool permissive_empty = true);

namespace detail {

// Structural-equality including lifetime fragments. Stronger than TypeUID
// (which is lifetime-erased). Used for invariant positions where subtype
// must collapse to equality.
//
// Elision rule: an empty lifetime on either side is treated as a wildcard
// (region inference will resolve it). Mutual outlives via the supplied
// graph also counts as equality — `'a: 'static, 'static: 'a` implies
// `'a == 'static` for Inv comparison purposes, so `&'static T` and
// `&'a T` are equal at an Inv position when both directions hold.
inline bool types_equal_with_lifetimes(TypeRef a, TypeRef b,
                                       const OutlivesAdj* adj = nullptr) {
    if (!a || !b) return a == b;
    // logos-core 1.3 (nested): `_` placeholder acts as a wildcard at any
    // position — variance/subtype walks through Vec<_> ≡ Vec<i32> by
    // treating the InferredType slot as compatible with whatever it
    // meets. types_compatible already accepts the same direction; this
    // is the variance-check parallel.
    if (a.kind() == LogosType::Kind::InferredType ||
        b.kind() == LogosType::Kind::InferredType) return true;
    if (a.kind() != b.kind()) return false;
    // PROBES lteqempty_site / lteqbothempty / lteqoneempty / ltmintfresh —
    // DOOR 1 of the elided-region minting question. `""` means both "elided
    // here" and "the same region as that other elided slot"; only the minting
    // site can tell them apart. Numbers in src/compiler/PROBES.md 2026-08-31.
    auto lt_eq = [&](std::string_view x, std::string_view y) {
        if (x.empty() || y.empty()) {
            (void)logos::probe::on("lteqempty_site");
            if (x.empty() && y.empty() && logos::probe::on("lteqbothempty"))
                return false;
            if (!(x.empty() && y.empty()) && logos::probe::on("lteqoneempty"))
                return false;
            if (logos::probe::on("ltmintfresh")) return false;
            return true;
        }
        if (x == y) return true;
        if (!adj) return false;
        // Mutual outlives → treated as equal.
        return outlives(x, y, *adj, /*permissive_empty=*/false) &&
               outlives(y, x, *adj, /*permissive_empty=*/false);
    };
    using K = LogosType::Kind;
    switch (a.kind()) {
        case K::Ref:
        case K::MutRef: {
            if (!lt_eq(a.lifetime(), b.lifetime())) return false;
            return types_equal_with_lifetimes(a.pointee(), b.pointee(), adj);
        }
        case K::Ptr:
            return types_equal_with_lifetimes(a.pointee(), b.pointee(), adj);
        case K::Tuple: {
            auto ae = a.tuple_elems();
            auto be = b.tuple_elems();
            if (ae.size() != be.size()) return false;
            for (size_t i = 0; i < ae.size(); ++i)
                if (!types_equal_with_lifetimes(ae[i], be[i], adj)) return false;
            return true;
        }
        case K::Array:
        case K::Slice:
            return types_equal_with_lifetimes(a.elem(), b.elem(), adj);
        case K::AssocType: {
            // B88: AssocType (e.g. T::Item<'a>) — compare trait/name/base
            // plus GAT lifetime_args. Two `T::Item<'a>` and `T::Item<'b>`
            // are NOT equal under lifetimes; the existing TypeUID erases
            // them.
            if (a.trait_name() != b.trait_name()) return false;
            if (a.assoc_type_name() != b.assoc_type_name()) return false;
            if (!types_equal_with_lifetimes(a.assoc_base(), b.assoc_base(), adj))
                return false;
            auto ag = a.gat_args();
            auto bg = b.gat_args();
            if (ag.size() != bg.size()) return false;
            for (size_t i = 0; i < ag.size(); ++i)
                if (!types_equal_with_lifetimes(ag[i], bg[i], adj)) return false;
            auto alts = a.lifetime_args();
            auto blts = b.lifetime_args();
            if (alts.size() != blts.size()) return false;
            for (size_t i = 0; i < alts.size(); ++i)
                if (!lt_eq(alts[i], blts[i])) return false;
            return true;
        }
        case K::FnPtr:
        case K::Closure: {
            // B81: types_equal_with_lifetimes must recurse through FnPtr to
            // see the inner Ref's lifetime. Previously fell through to the
            // default (TypeUID), which is lifetime-erased — so two FnPtrs
            // differing only by an inner Ref's lifetime were collapsed,
            // and the surrounding subtype check skipped the variance.
            auto ap = a.closure_params();
            auto bp = b.closure_params();
            if (ap.size() != bp.size()) return false;
            for (size_t i = 0; i < ap.size(); ++i)
                if (!types_equal_with_lifetimes(ap[i], bp[i], adj)) return false;
            return types_equal_with_lifetimes(a.closure_ret(), b.closure_ret(), adj);
        }
        case K::Struct:
        case K::ZonedStruct:
        case K::Enum: {
            if (a.struct_name() != b.struct_name() &&
                a.enum_name()   != b.enum_name()) return false;
            if (a.pkg_name() != b.pkg_name()) return false;
            auto ata = a.type_args();
            auto bta = b.type_args();
            if (ata.size() != bta.size()) return false;
            for (size_t i = 0; i < ata.size(); ++i)
                if (!types_equal_with_lifetimes(ata[i], bta[i], adj)) return false;
            auto alts = a.lifetime_args();
            auto blts = b.lifetime_args();
            if (alts.size() != blts.size()) return false;
            for (size_t i = 0; i < alts.size(); ++i)
                if (!lt_eq(alts[i], blts[i])) return false;
            return true;
        }
        default:
            // Primitives + TypeVar + closures + etc.: identity via TypeUID
            // is sufficient (no lifetime fragments to disagree on).
            return types_equal(a, b);
    }
}

// Subtype a position with variance v.
inline bool subtype_at(Variance v, TypeRef sub, TypeRef sup,
                       const OutlivesAdj& adj,
                       const DefVarianceTable& vars,
                       int depth,
                       bool permissive_empty)
{
    switch (v) {
        case Variance::BiVar:  return true;
        case Variance::Co:     return subtype(sub, sup, adj, vars, depth + 1, permissive_empty);
        case Variance::Contra: return subtype(sup, sub, adj, vars, depth + 1, permissive_empty);
        case Variance::Inv:    return types_equal_with_lifetimes(sub, sup, &adj);
    }
    return false;
}

inline bool lifetime_at(Variance v,
                        std::string_view sub_lt,
                        std::string_view sup_lt,
                        const OutlivesAdj& adj,
                        bool permissive_empty)
{
    switch (v) {
        case Variance::BiVar: return true;
        case Variance::Co:    return outlives(sub_lt, sup_lt, adj, permissive_empty);
        case Variance::Contra: return outlives(sup_lt, sub_lt, adj, permissive_empty);
        // PROBES ltinvarm_site / ltinvempty_site / ltinvempty — DOOR 2, and
        // the census says it is nearly dead: 9 arrivals, 0 with an empty side.
        // src/compiler/PROBES.md 2026-08-31.
        case Variance::Inv: {
            (void)logos::probe::on("ltinvarm_site");
            if (sub_lt.empty() || sup_lt.empty()) {
                (void)logos::probe::on("ltinvempty_site");
                if (logos::probe::on("ltmintfresh")) return false;
                if (logos::probe::on("ltinvempty")) return false;
            }
            return outlives_norm(sub_lt) == outlives_norm(sup_lt);
        }
    }
    return false;
}

} // namespace detail

// Definition.
//
// Semantics: returns true when `sub` is variance-compatible with `sup` as far
// as LIFETIME STRUCTURE goes. Caller is responsible for the structural /
// kind-level compatibility check (e.g. via types_compatible) — this function
// only enforces the additional variance constraints that types_compatible
// (a lifetime-erased check) cannot see.
//
// Concretely: returns false only when sub/sup share a kind that has a
// variance rule and the lifetime-aware structure disagrees.
inline bool subtype(TypeRef sub, TypeRef sup,
                    const OutlivesAdj& adj,
                    const DefVarianceTable& vars,
                    int depth,
                    bool permissive_empty)
{
    if (depth > 64) return true;  // give up gracefully — caller has compat
    if (!sub || !sup) return true;
    using K = LogosType::Kind;
    if (detail::types_equal_with_lifetimes(sub, sup, &adj)) return true;

    // Different kinds: caller's compat check handles legitimate cross-kind
    // coercions (e.g. IntLit → i32, &mut → &, Vec → slice). Don't impose a
    // variance constraint there.
    if (sub.kind() != sup.kind()) return true;

    switch (sub.kind()) {
        case K::Ref: {
            // Co in lifetime, Co in pointee.
            if (!detail::lifetime_at(Variance::Co, sub.lifetime(), sup.lifetime(), adj, permissive_empty))
                return false;
            return subtype(sub.pointee(), sup.pointee(), adj, vars, depth + 1, permissive_empty);
        }
        case K::MutRef: {
            // Co in lifetime, Inv in pointee.
            if (!detail::lifetime_at(Variance::Co, sub.lifetime(), sup.lifetime(), adj, permissive_empty))
                return false;
            return detail::types_equal_with_lifetimes(sub.pointee(), sup.pointee(), &adj);
        }
        case K::Ptr: {
            // B84: raw pointer variance:
            //   *const T — Co in pointee (matches Rust)
            //   *mut T   — Inv in pointee
            // Both: no lifetime tracking on Ptr itself.
            if (sub.mut_ptr() != sup.mut_ptr()) return true;  // shape diff
            if (sub.mut_ptr())
                return detail::types_equal_with_lifetimes(sub.pointee(), sup.pointee(), &adj);
            return subtype(sub.pointee(), sup.pointee(), adj, vars, depth + 1, permissive_empty);
        }
        case K::Tuple: {
            auto se = sub.tuple_elems();
            auto pe = sup.tuple_elems();
            if (se.size() != pe.size()) return true;  // shape diff → compat
            for (size_t i = 0; i < se.size(); ++i)
                if (!subtype(se[i], pe[i], adj, vars, depth + 1, permissive_empty)) return false;
            return true;
        }
        case K::Array:
        case K::Slice:
            return subtype(sub.elem(), sup.elem(), adj, vars, depth + 1, permissive_empty);
        case K::Struct:
        case K::ZonedStruct: {
            if (sub.struct_name() != sup.struct_name()) return true;
            if (sub.pkg_name() != sup.pkg_name()) return true;
            std::string key = std::string(sub.pkg_name()) + (sub.pkg_name().empty() ? "" : ".")
                            + std::string(sub.struct_name());
            auto vit = vars.find(key);
            const VarianceMap* vm = (vit == vars.end()) ? nullptr : &vit->second;
            auto var_for = [&](size_t i, bool is_lifetime) -> Variance {
                (void)i; (void)is_lifetime;
                if (!vm) return Variance::Co;
                std::string ikey = (is_lifetime ? "@" : "#") + std::to_string(i);
                auto it = vm->find(ikey);
                if (it != vm->end()) return it->second;
                return Variance::Co;
            };
            auto sta = sub.type_args();
            auto pta = sup.type_args();
            if (sta.size() != pta.size()) return true;
            for (size_t i = 0; i < sta.size(); ++i) {
                if (!detail::subtype_at(var_for(i, false), sta[i], pta[i], adj, vars, depth + 1, permissive_empty))
                    return false;
            }
            auto sl = sub.lifetime_args();
            auto pl = sup.lifetime_args();
            if (sl.size() != pl.size()) return true;
            for (size_t i = 0; i < sl.size(); ++i) {
                if (!detail::lifetime_at(var_for(i, true), sl[i], pl[i], adj, permissive_empty))
                    return false;
            }
            return true;
        }
        case K::Enum: {
            if (sub.enum_name() != sup.enum_name()) return true;
            if (sub.pkg_name() != sup.pkg_name()) return true;
            auto sta = sub.type_args();
            auto pta = sup.type_args();
            if (sta.size() != pta.size()) return true;
            // B81: enum is currently treated as Co in every type-arg + lt-arg
            // position. Variance-table per enum def isn't wired up yet (same
            // as Struct, conservatively: fall back to Co — matches the
            // Co-shape of Option/Result/Box). Lifetime arg variance follows.
            for (size_t i = 0; i < sta.size(); ++i)
                if (!subtype(sta[i], pta[i], adj, vars, depth + 1, permissive_empty)) return false;
            auto sl = sub.lifetime_args();
            auto pl = sup.lifetime_args();
            if (sl.size() != pl.size()) return true;
            for (size_t i = 0; i < sl.size(); ++i)
                if (!detail::lifetime_at(Variance::Co, sl[i], pl[i], adj, permissive_empty))
                    return false;
            return true;
        }
        case K::FnPtr:
        case K::Closure: {
            // Contra in params, Co in ret. Closure shares the same variance
            // shape as FnPtr — param/ret accessors are uniform.
            auto sp = sub.closure_params();
            auto pp = sup.closure_params();
            if (sp.size() != pp.size()) return true;
            for (size_t i = 0; i < sp.size(); ++i)
                if (!subtype(pp[i], sp[i], adj, vars, depth + 1, permissive_empty)) return false;
            return subtype(sub.closure_ret(), sup.closure_ret(), adj, vars, depth + 1, permissive_empty);
        }
        case K::AssocType: {
            // B88: GAT lifetime args must match (Inv by default — GAT lt
            // variance isn't user-controllable). Type args follow Co.
            if (sub.trait_name() != sup.trait_name()) return true;
            if (sub.assoc_type_name() != sup.assoc_type_name()) return true;
            if (!subtype(sub.assoc_base(), sup.assoc_base(), adj, vars, depth + 1, permissive_empty))
                return false;
            auto sg = sub.gat_args();
            auto pg = sup.gat_args();
            if (sg.size() != pg.size()) return true;
            for (size_t i = 0; i < sg.size(); ++i)
                if (!subtype(sg[i], pg[i], adj, vars, depth + 1, permissive_empty)) return false;
            auto sl = sub.lifetime_args();
            auto pl = sup.lifetime_args();
            if (sl.size() != pl.size()) return true;
            for (size_t i = 0; i < sl.size(); ++i)
                if (!detail::lifetime_at(Variance::Inv, sl[i], pl[i], adj, permissive_empty))
                    return false;
            return true;
        }
        default:
            return true;
    }
}

} // namespace logos::compiler
