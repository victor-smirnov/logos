// Minimal Datalog-style trait resolver engine. Scope: internal to the
// compiler, not a Datalog feature for end users. See
// docs/track3-gaps/SPRINT-5-DATALOG-DESIGN.md for the motivation.
//
// Phase 1 (this file): pure-data engine over string-keyed relations
// with semi-naive fixpoint + cycle guard. No type-system integration
// yet — Phase 2 will load facts from sema and replace
// mono_has_impl_recursive.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace logos::compiler::trait_engine {

// Identifier for a trait, type, impl. All string-keyed for Phase 1 —
// the resolver works on canonical names (e.g. "pkg.Trait", "i32",
// "Vec<i32>"). Substitution-aware matching lives in Phase 2.
using TraitName = std::string;
using TypeName  = std::string;
using ImplId    = std::uint32_t;

constexpr ImplId NO_IMPL = 0;

// A direct impl fact: type X implements trait T (impl id encodes which
// `impl T for X` clause produced this; used only for diagnostics).
struct ImplFact {
    TraitName trait_name;
    TypeName  type_name;
    ImplId    impl_id = NO_IMPL;
};

// A blanket impl: forall T satisfying `bound`, `target` is implemented
// for T. Concrete representation chosen so multiple types can derive
// the same target trait without enumerating them eagerly.
struct BlanketImplFact {
    TraitName target_trait;   // trait being implemented
    TraitName bound_trait;    // bound the impl type must satisfy
    ImplId    impl_id = NO_IMPL;
};

// An auto-trait rule: "all types implement T" (modulo negative
// constraints — those go via NegativeBound). Used for marker traits
// like Copy / Send / Sync and for closure-as-Fn family. For Phase 1
// we keep it unconditional and let the loader carve out exceptions.
struct AutoImplFact {
    TraitName trait_name;
};

// A type "shape" predicate — a callable that decides whether the given
// type name matches a structural pattern (e.g. "is a closure type").
// Phase 1 represents shapes by a tag string ("closure", "scalar",
// "struct", "enum"); Phase 2 will refine when we wire up actual types.
using ShapePredicate = std::function<bool(std::string_view type_name)>;

// A shape-conditioned auto-impl: "every type of shape S implements T".
// This is the building block for `auto impl Fn for <every closure>`.
struct ShapeAutoImplFact {
    TraitName        trait_name;
    std::string      shape_tag;       // diagnostic
    ShapePredicate   shape;            // actual check
};

class TraitEngine {
public:
    TraitEngine() = default;

    // ── Facts ────────────────────────────────────────────────────
    ImplId add_impl(TraitName trait, TypeName type_name);
    ImplId add_blanket(TraitName target_trait, TraitName bound_trait);
    void   add_auto_impl(TraitName trait);
    void   add_shape_auto_impl(TraitName trait, std::string shape_tag,
                               ShapePredicate pred);

    // Negative carve-out: "type X does NOT implement trait T" — beats
    // any auto / shape-auto fact. (Phase 1 implementation: a simple
    // {trait,type} set checked before deriving new facts.)
    void add_negative(TraitName trait, TypeName type_name);

    // ── Queries ──────────────────────────────────────────────────
    //
    // satisfies(T, X) — does type X implement trait T?
    //
    // Internally runs the fixpoint up to the point where the question
    // is answered (or refuted). Results are memoised across queries
    // within one engine instance.
    bool satisfies(const TraitName& trait, const TypeName& type_name);

    // resolve(T, X) → impl id (NO_IMPL if no direct/derived impl).
    // Used by sema to know which impl to dispatch through.
    ImplId resolve(const TraitName& trait, const TypeName& type_name);

    // ── Diagnostics ──────────────────────────────────────────────
    std::vector<std::string> trace_satisfies(const TraitName& trait,
                                              const TypeName& type_name);

    // Stats: number of facts in each relation, derivation count.
    struct Stats {
        std::size_t direct_facts        = 0;
        std::size_t blanket_facts       = 0;
        std::size_t auto_facts          = 0;
        std::size_t shape_auto_facts    = 0;
        std::size_t negative_facts      = 0;
        std::size_t derived_facts       = 0;
        std::size_t fixpoint_rounds     = 0;
    };
    Stats stats() const { return stats_; }
    void clear_derived();   // invalidate memo (used in tests)

private:
    struct PairHash {
        std::size_t operator()(const std::pair<TraitName, TypeName>& k) const {
            std::size_t h = std::hash<TraitName>{}(k.first);
            return h ^ (std::hash<TypeName>{}(k.second) + 0x9e3779b97f4a7c15ULL +
                       (h << 6) + (h >> 2));
        }
    };

    // Direct & derived impl facts. Keyed by (trait, type) → impl_id
    // so the same query is O(1) after the first derivation.
    std::unordered_map<std::pair<TraitName, TypeName>, ImplId, PairHash> direct_;
    std::unordered_set<std::pair<TraitName, TypeName>, PairHash>         negative_;
    std::vector<BlanketImplFact>      blankets_;
    std::vector<AutoImplFact>          autos_;
    std::vector<ShapeAutoImplFact>     shape_autos_;

    // Memo of "does X impl T?" queries, including derivations. Keyed
    // by (trait, type). A miss means "not yet computed"; a hit returns
    // the cached impl_id (NO_IMPL == no impl). Refreshed by the
    // fixpoint when new facts are added between queries.
    std::unordered_map<std::pair<TraitName, TypeName>, ImplId, PairHash> memo_;

    // Cycle-guard for recursive bound resolution. A query that re-asks
    // itself (e.g. through a circular blanket) returns NO_IMPL without
    // looping, then the outer rule decides.
    std::unordered_set<std::pair<TraitName, TypeName>, PairHash> in_flight_;

    Stats stats_{};

    ImplId next_impl_id_ = 1;

    // Internal: resolve impl_id for (trait, type) using direct facts
    // + blanket + auto + shape-auto. Records a memo on success.
    ImplId resolve_impl_(const TraitName& trait, const TypeName& type_name);
};

} // namespace logos::compiler::trait_engine
