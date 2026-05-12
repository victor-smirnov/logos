#pragma once
// B64: per-type-parameter variance and the variance-aware subtype relation.
//
// Variance describes how a generic parameter's position behaves under
// substitution:
//   - Co     (covariant)     : Foo<&'long T> :> Foo<&'short T> when 'long: 'short
//                              (or T_sub :> T_super in nominal-subtype contexts)
//   - Contra (contravariant) : reverses the direction (fn-arg position)
//   - Inv    (invariant)     : both directions required (mut-ref content)
//   - BiVar  (bivariant)     : parameter doesn't actually appear in any
//                              non-phantom field; substitution is unconstrained
//
// The lattice meet is "the more-restrictive of two demands":
//   BiVar ∧ X = X
//   Co ∧ Co = Co; Contra ∧ Contra = Contra
//   Co ∧ Contra = Inv (both directions demanded)
//   Inv ∧ X = Inv
//
// Composition (outer ∘ inner) handles nesting: when a parameter P appears in
// a field of type Wrapper<P>, and the field itself is held in position with
// variance `outer`, while Wrapper uses P at variance `inner`, the parameter's
// effective variance in the outer struct is `outer ∘ inner`.

#include <cstdint>
#include <unordered_map>
#include <string>
#include <vector>

namespace logos::compiler {

enum class Variance : uint8_t { BiVar = 0, Co = 1, Contra = 2, Inv = 3 };

inline Variance variance_meet(Variance a, Variance b) noexcept {
    if (a == Variance::BiVar) return b;
    if (b == Variance::BiVar) return a;
    if (a == b) return a;
    return Variance::Inv;
}

inline Variance variance_compose(Variance outer, Variance inner) noexcept {
    if (outer == Variance::BiVar || inner == Variance::BiVar) return Variance::BiVar;
    if (outer == Variance::Inv || inner == Variance::Inv) return Variance::Inv;
    // Co ∘ Co = Co; Contra ∘ Co = Contra; Co ∘ Contra = Contra; Contra ∘ Contra = Co
    bool flip = (outer == Variance::Contra) ^ (inner == Variance::Contra);
    return flip ? Variance::Contra : Variance::Co;
}

inline const char* variance_name(Variance v) noexcept {
    switch (v) {
        case Variance::BiVar:  return "BiVar";
        case Variance::Co:     return "Co";
        case Variance::Contra: return "Contra";
        case Variance::Inv:    return "Inv";
    }
    return "?";
}

// Per-type-parameter variance table for a struct / enum. Indexed by the
// parameter's NAME (e.g. "T", "'a"). Lifetime params share the same map.
using VarianceMap = std::unordered_map<std::string, Variance>;

// Per-type-def variances, keyed by "pkg.Type" (or bare "Type" for built-ins).
using DefVarianceTable = std::unordered_map<std::string, VarianceMap>;

} // namespace logos::compiler
