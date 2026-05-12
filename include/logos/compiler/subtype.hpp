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
bool subtype(TypeRef sub, TypeRef sup,
             const OutlivesAdj& adj,
             const DefVarianceTable& vars,
             int depth = 0);

namespace detail {

// Structural-equality including lifetime fragments. Stronger than TypeUID
// (which is lifetime-erased). Used for invariant positions where subtype
// must collapse to equality.
inline bool types_equal_with_lifetimes(TypeRef a, TypeRef b) {
    if (!a || !b) return a == b;
    if (a.kind() != b.kind()) return false;
    using K = LogosType::Kind;
    switch (a.kind()) {
        case K::Ref:
        case K::MutRef: {
            if (std::string(a.lifetime()) != std::string(b.lifetime()))
                return false;
            return types_equal_with_lifetimes(a.pointee(), b.pointee());
        }
        case K::Ptr:
            return types_equal_with_lifetimes(a.pointee(), b.pointee());
        case K::Tuple: {
            auto ae = a.tuple_elems();
            auto be = b.tuple_elems();
            if (ae.size() != be.size()) return false;
            for (size_t i = 0; i < ae.size(); ++i)
                if (!types_equal_with_lifetimes(ae[i], be[i])) return false;
            return true;
        }
        case K::Array:
        case K::Slice:
            return types_equal_with_lifetimes(a.elem(), b.elem());
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
                if (!types_equal_with_lifetimes(ata[i], bta[i])) return false;
            auto alts = a.lifetime_args();
            auto blts = b.lifetime_args();
            if (alts.size() != blts.size()) return false;
            for (size_t i = 0; i < alts.size(); ++i)
                if (std::string(alts[i]) != std::string(blts[i])) return false;
            return true;
        }
        default:
            // Primitives + TypeVar + closures + etc.: identity via TypeUID
            // is sufficient (no lifetime fragments to disagree on).
            return types_equal(a, b);
    }
}

// Subtype a position with variance v.
//   Co     → subtype(sub, sup)
//   Contra → subtype(sup, sub)
//   Inv    → equal-with-lifetimes
//   BiVar  → trivially OK
inline bool subtype_at(Variance v, TypeRef sub, TypeRef sup,
                       const OutlivesAdj& adj,
                       const DefVarianceTable& vars,
                       int depth)
{
    switch (v) {
        case Variance::BiVar:  return true;
        case Variance::Co:     return subtype(sub, sup, adj, vars, depth + 1);
        case Variance::Contra: return subtype(sup, sub, adj, vars, depth + 1);
        case Variance::Inv:    return types_equal_with_lifetimes(sub, sup);
    }
    return false;
}

inline bool lifetime_at(Variance v,
                        std::string_view sub_lt,
                        std::string_view sup_lt,
                        const OutlivesAdj& adj)
{
    switch (v) {
        case Variance::BiVar: return true;
        case Variance::Co:    return outlives(sub_lt, sup_lt, adj);
        case Variance::Contra: return outlives(sup_lt, sub_lt, adj);
        case Variance::Inv:
            return outlives_norm(sub_lt) == outlives_norm(sup_lt);
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
                    int depth)
{
    if (depth > 64) return true;  // give up gracefully — caller has compat
    if (!sub || !sup) return true;
    using K = LogosType::Kind;
    if (detail::types_equal_with_lifetimes(sub, sup)) return true;

    // Different kinds: caller's compat check handles legitimate cross-kind
    // coercions (e.g. IntLit → i32, &mut → &, Vec → slice). Don't impose a
    // variance constraint there.
    if (sub.kind() != sup.kind()) return true;

    switch (sub.kind()) {
        case K::Ref: {
            // Co in lifetime, Co in pointee.
            if (!detail::lifetime_at(Variance::Co, sub.lifetime(), sup.lifetime(), adj))
                return false;
            return subtype(sub.pointee(), sup.pointee(), adj, vars, depth + 1);
        }
        case K::MutRef: {
            // Co in lifetime, Inv in pointee.
            if (!detail::lifetime_at(Variance::Co, sub.lifetime(), sup.lifetime(), adj))
                return false;
            return detail::types_equal_with_lifetimes(sub.pointee(), sup.pointee());
        }
        case K::Ptr:
            // Raw pointers: Inv in pointee, but no lifetime tracking either.
            // Defer to types_equal_with_lifetimes which ignores lifetime on Ptr.
            return true;
        case K::Tuple: {
            auto se = sub.tuple_elems();
            auto pe = sup.tuple_elems();
            if (se.size() != pe.size()) return true;  // shape diff → compat
            for (size_t i = 0; i < se.size(); ++i)
                if (!subtype(se[i], pe[i], adj, vars, depth + 1)) return false;
            return true;
        }
        case K::Array:
        case K::Slice:
            return subtype(sub.elem(), sup.elem(), adj, vars, depth + 1);
        case K::Struct:
        case K::ZonedStruct: {
            if (sub.struct_name() != sup.struct_name()) return true;
            if (sub.pkg_name() != sup.pkg_name()) return true;
            // Variance lookup keyed by "pkg.Name". `vars` may carry an
            // ordered variance list under the same key (one entry per param)
            // when compute_variances has populated it. Default per-param =
            // Co (matches Rust default for most container-like types; INV
            // cases like Cell<T> need explicit per-param entries).
            std::string key = std::string(sub.pkg_name()) + (sub.pkg_name().empty() ? "" : ".")
                            + std::string(sub.struct_name());
            auto vit = vars.find(key);
            const VarianceMap* vm = (vit == vars.end()) ? nullptr : &vit->second;
            auto var_for = [&](size_t i, bool is_lifetime) -> Variance {
                (void)i; (void)is_lifetime;
                if (!vm) return Variance::Co;
                // No name → fall back to Co. compute_variances may key by
                // synthetic "#0", "#1" indices; check for those.
                std::string ikey = (is_lifetime ? "@" : "#") + std::to_string(i);
                auto it = vm->find(ikey);
                if (it != vm->end()) return it->second;
                return Variance::Co;
            };
            auto sta = sub.type_args();
            auto pta = sup.type_args();
            if (sta.size() != pta.size()) return true;
            for (size_t i = 0; i < sta.size(); ++i) {
                if (!detail::subtype_at(var_for(i, false), sta[i], pta[i], adj, vars, depth + 1))
                    return false;
            }
            auto sl = sub.lifetime_args();
            auto pl = sup.lifetime_args();
            if (sl.size() != pl.size()) return true;
            for (size_t i = 0; i < sl.size(); ++i) {
                if (!detail::lifetime_at(var_for(i, true), sl[i], pl[i], adj))
                    return false;
            }
            return true;
        }
        case K::Enum: {
            if (sub.enum_name() != sup.enum_name()) return true;
            auto sta = sub.type_args();
            auto pta = sup.type_args();
            if (sta.size() != pta.size()) return true;
            for (size_t i = 0; i < sta.size(); ++i)
                if (!subtype(sta[i], pta[i], adj, vars, depth + 1)) return false;
            return true;
        }
        case K::FnPtr: {
            // Contra in params, Co in ret.
            auto sp = sub.closure_params();
            auto pp = sup.closure_params();
            if (sp.size() != pp.size()) return true;
            for (size_t i = 0; i < sp.size(); ++i)
                if (!subtype(pp[i], sp[i], adj, vars, depth + 1)) return false;
            return subtype(sub.closure_ret(), sup.closure_ret(), adj, vars, depth + 1);
        }
        default:
            return true;
    }
}

} // namespace logos::compiler
