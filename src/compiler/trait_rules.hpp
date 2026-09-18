// Trait resolution for mono, as Datalog rules (ADR 0028 S2, #421).
//
// Same questions as trait_engine::TraitEngine, answered by
// src/compiler/dl/rules/traits.dl. The rules have no negation, so the database
// is driven incrementally: every satisfies() adds a query row and runs to the
// new fixpoint. Traits answered by a C++ predicate (closures, slices, auto
// traits) are evaluated only for the (trait, type) pairs the rules demand.
#pragma once

#include "dl/dl.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace logos::compiler {

class TraitRules {
public:
    using Predicate = std::function<bool(std::string_view type_name)>;

    TraitRules();
    ~TraitRules();

    void add_impl(const std::string& trait, const std::string& type_name);
    // All of `bounds` must hold for the same type; empty means every type.
    void add_blanket(const std::string& trait, const std::vector<std::string>& bounds);
    // `pred` decides trait for a type; several predicates for one trait are
    // alternatives (any may answer yes).
    void add_predicate(const std::string& trait, Predicate pred);

    bool satisfies(const std::string& trait, const std::string& type_name);
    // The derivation of impls(trait, type), or a one-line "no derivation".
    std::string explain(const std::string& trait, const std::string& type_name);

private:
    uint32_t rel_(std::string_view name) const;
    void     insert_(uint32_t rel, std::initializer_list<dl::Value> row);
    dl::Value sym_(std::string_view s) { return syms_.intern(s); }

    dl::Symbols                    syms_;
    std::unique_ptr<dl::Program>   prog_;
    std::unique_ptr<dl::Database>  db_;
    std::unordered_map<dl::Value, std::vector<Predicate>> preds_;
    uint32_t next_blanket_ = 1;
    size_t   pred_need_seen_ = 0;   // rows of pred_need already answered
    bool     stale_ = true;         // rows inserted since the last run()
    uint32_t r_impl_direct_, r_blanket_, r_blanket_bound_, r_blanket_nbounds_,
             r_pred_backed_, r_pred_ok_, r_query_, r_pred_need_, r_impls_;
};

} // namespace logos::compiler
