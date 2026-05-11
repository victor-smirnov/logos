// Implementation of the minimal Datalog-style trait resolver engine.
// See trait_engine.hpp + docs/track3-gaps/SPRINT-5-DATALOG-DESIGN.md.
//
// Phase 1 semantics (this file):
//
// satisfies(T, X) is true iff at least one of these derivations
// succeeds:
//
//   (D) direct fact:        impls(T, X)
//   (B) blanket impl:       blanket(T ← Tb)  and  satisfies(Tb, X)
//   (A) auto impl:          auto(T)          and  not negative(T, X)
//   (S) shape-auto impl:    shape_auto(T, S) and  S(X)  and not negative(T, X)
//
// The fixpoint isn't strictly needed since this Phase has only one
// derivation depth that can multiply (blanket chains). We still
// memoise + cycle-guard so a circular blanket bound terminates with
// "no derivation through this path" rather than blowing the stack.

#include "trait_engine.hpp"

#include <algorithm>
#include <sstream>

namespace logos::compiler::trait_engine {

// ── Fact-adding methods ──────────────────────────────────────────

ImplId TraitEngine::add_impl(TraitName trait, TypeName type_name) {
    auto key = std::make_pair(std::move(trait), std::move(type_name));
    auto it  = direct_.find(key);
    if (it != direct_.end()) return it->second;
    auto id = next_impl_id_++;
    direct_.emplace(std::move(key), id);
    memo_.clear();   // any cached "no" might now be "yes"
    ++stats_.direct_facts;
    return id;
}

ImplId TraitEngine::add_blanket(TraitName target_trait, TraitName bound_trait) {
    auto id = next_impl_id_++;
    blankets_.push_back({std::move(target_trait), std::move(bound_trait), id});
    memo_.clear();
    ++stats_.blanket_facts;
    return id;
}

void TraitEngine::add_auto_impl(TraitName trait) {
    autos_.push_back({std::move(trait)});
    memo_.clear();
    ++stats_.auto_facts;
}

void TraitEngine::add_shape_auto_impl(TraitName trait, std::string shape_tag,
                                       ShapePredicate pred) {
    shape_autos_.push_back({std::move(trait), std::move(shape_tag), std::move(pred)});
    memo_.clear();
    ++stats_.shape_auto_facts;
}

void TraitEngine::add_negative(TraitName trait, TypeName type_name) {
    negative_.emplace(std::move(trait), std::move(type_name));
    memo_.clear();
    ++stats_.negative_facts;
}

void TraitEngine::clear_derived() {
    memo_.clear();
    in_flight_.clear();
}

// ── Query path ───────────────────────────────────────────────────

ImplId TraitEngine::resolve_impl_(const TraitName& trait, const TypeName& type_name) {
    auto key = std::make_pair(trait, type_name);

    // Negative facts beat everything (Phase 1 simple-priority order).
    if (negative_.count(key)) return NO_IMPL;

    // Memoisation: includes negative results so repeated queries don't
    // re-walk the rules.
    if (auto mit = memo_.find(key); mit != memo_.end()) return mit->second;

    // Cycle guard. Reaching the same query through a derivation chain
    // returns NO_IMPL for this path; outer rules then try alternatives.
    if (!in_flight_.insert(key).second) return NO_IMPL;
    struct Pop {
        TraitEngine* self;
        std::pair<TraitName, TypeName> k;
        ~Pop() { self->in_flight_.erase(k); }
    } pop{this, key};

    // (D) direct fact.
    if (auto dit = direct_.find(key); dit != direct_.end()) {
        memo_[key] = dit->second;
        return dit->second;
    }

    // (B) blanket: walk all blankets targeting `trait`, recursively
    // check the bound. First match wins (Phase 1 — coherence later).
    for (auto& b : blankets_) {
        if (b.target_trait != trait) continue;
        auto bound_impl = resolve_impl_(b.bound_trait, type_name);
        if (bound_impl == NO_IMPL) continue;
        ++stats_.derived_facts;
        memo_[key] = b.impl_id;
        return b.impl_id;
    }

    // (A) auto impl: trait is in the auto list, and there's no
    // negative carve-out for this pair (checked at top).
    for (auto& a : autos_) {
        if (a.trait_name != trait) continue;
        // We need an ImplId for the derived "auto" fact. Reserve a
        // fresh one per (trait, type) pair the first time it is
        // queried so callers can compare ids deterministically.
        auto id = next_impl_id_++;
        ++stats_.derived_facts;
        memo_[key] = id;
        return id;
    }

    // (S) shape-auto: any shape predicate matches the type's name.
    for (auto& s : shape_autos_) {
        if (s.trait_name != trait) continue;
        if (!s.shape) continue;
        if (!s.shape(type_name)) continue;
        auto id = next_impl_id_++;
        ++stats_.derived_facts;
        memo_[key] = id;
        return id;
    }

    memo_[key] = NO_IMPL;
    return NO_IMPL;
}

bool TraitEngine::satisfies(const TraitName& trait, const TypeName& type_name) {
    ++stats_.fixpoint_rounds;
    return resolve_impl_(trait, type_name) != NO_IMPL;
}

ImplId TraitEngine::resolve(const TraitName& trait, const TypeName& type_name) {
    return resolve_impl_(trait, type_name);
}

std::vector<std::string>
TraitEngine::trace_satisfies(const TraitName& trait, const TypeName& type_name) {
    // Phase 1 trace: lightweight breadcrumb showing which rule fired.
    // Not the full derivation graph (which would need an explicit
    // derivation-tree representation alongside memo_).
    std::vector<std::string> out;
    auto key = std::make_pair(trait, type_name);

    if (negative_.count(key)) {
        out.push_back("negative carve-out: !impls(" + trait + ", " + type_name + ")");
        return out;
    }

    if (auto dit = direct_.find(key); dit != direct_.end()) {
        out.push_back("direct: impls(" + trait + ", " + type_name + ") = #" +
                       std::to_string(dit->second));
        return out;
    }

    for (auto& b : blankets_) {
        if (b.target_trait != trait) continue;
        if (satisfies(b.bound_trait, type_name)) {
            out.push_back("blanket #" + std::to_string(b.impl_id) +
                           ": " + trait + " ← " + b.bound_trait);
            auto inner = trace_satisfies(b.bound_trait, type_name);
            for (auto& s : inner) out.push_back("  " + s);
            return out;
        }
    }

    for (auto& a : autos_) {
        if (a.trait_name != trait) continue;
        out.push_back("auto: " + trait);
        return out;
    }

    for (auto& s : shape_autos_) {
        if (s.trait_name != trait) continue;
        if (s.shape && s.shape(type_name)) {
            out.push_back("shape-auto: " + trait + " for shape '" + s.shape_tag + "'");
            return out;
        }
    }

    out.push_back("no derivation: !satisfies(" + trait + ", " + type_name + ")");
    return out;
}

} // namespace logos::compiler::trait_engine
