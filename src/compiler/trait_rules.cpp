// See trait_rules.hpp.
#include "trait_rules.hpp"

#include "dl/dl_rules.hpp"

#include <cstdio>
#include <cstdlib>

namespace logos::compiler {

namespace {

// traits.dl is part of the compiler binary; failing to load it is a build
// defect, not a user error, so it aborts with the parser's message.
std::unique_ptr<dl::Program> load_traits(dl::Symbols& syms) {
    auto text = dl::embedded_rule_file("traits.dl");
    if (!text) {
        std::fprintf(stderr, "logosc: internal: traits.dl is not embedded\n");
        std::abort();
    }
    dl::IncludeResolver inc = [](std::string_view n) -> std::optional<std::string> {
        if (auto t = dl::embedded_rule_file(n)) return std::string(*t);
        return std::nullopt;
    };
    auto p = dl::Program::parse(*text, "traits.dl", syms, inc);
    if (!p) {
        std::fprintf(stderr, "logosc: internal: %s\n", p.error().str().c_str());
        std::abort();
    }
    if (p->has_negation()) {
        std::fprintf(stderr, "logosc: internal: traits.dl must stay negation-free "
                             "(it is evaluated incrementally)\n");
        std::abort();
    }
    return std::make_unique<dl::Program>(std::move(*p));
}

} // namespace

TraitRules::TraitRules() {
    prog_ = load_traits(syms_);
    db_   = std::make_unique<dl::Database>(*prog_, syms_);
    r_impl_direct_     = rel_("impl_direct");
    r_blanket_         = rel_("blanket");
    r_blanket_bound_   = rel_("blanket_bound");
    r_blanket_nbounds_ = rel_("blanket_nbounds");
    r_pred_backed_     = rel_("pred_backed");
    r_pred_ok_         = rel_("pred_ok");
    r_query_           = rel_("query");
    r_pred_need_       = rel_("pred_need");
    r_impls_           = rel_("impls");
}

TraitRules::~TraitRules() = default;

uint32_t TraitRules::rel_(std::string_view name) const {
    auto r = prog_->relation(name);
    if (!r) {
        std::fprintf(stderr, "logosc: internal: traits.dl declares no '%.*s'\n",
                     static_cast<int>(name.size()), name.data());
        std::abort();
    }
    return *r;
}

void TraitRules::insert_(uint32_t rel, std::initializer_list<dl::Value> row) {
    if (db_->insert(rel, std::span<const dl::Value>(row.begin(), row.size()))) stale_ = true;
}

void TraitRules::add_impl(const std::string& trait, const std::string& type_name) {
    insert_(r_impl_direct_, {sym_(trait), sym_(type_name)});
}

void TraitRules::add_blanket(const std::string& trait, const std::vector<std::string>& bounds) {
    dl::Value id = next_blanket_++;
    insert_(r_blanket_, {id, sym_(trait)});
    for (dl::Value k = 0; k < bounds.size(); ++k)
        insert_(r_blanket_bound_, {id, k, sym_(bounds[k]), k + 1});
    insert_(r_blanket_nbounds_, {id, static_cast<dl::Value>(bounds.size())});
}

void TraitRules::add_predicate(const std::string& trait, Predicate pred) {
    dl::Value t = sym_(trait);
    insert_(r_pred_backed_, {t});
    preds_[t].push_back(std::move(pred));
}

bool TraitRules::satisfies(const std::string& trait, const std::string& type_name) {
    dl::Value t = sym_(trait), x = sym_(type_name);
    const dl::Value q[] = {t, x};
    // Monotone: a yes can never turn into a no.
    if (db_->relation(r_impls_).contains(q)) return true;
    insert_(r_query_, {t, x});
    while (stale_) {
        db_->run();
        stale_ = false;
        // Answer the predicate demands the last run produced; a yes is a new
        // input row, so the loop runs again.
        //
        // A predicate may re-enter satisfies() (the auto-trait one asks mono,
        // which asks here). The row is claimed BEFORE the call, so a nested
        // call starts at the next row instead of re-evaluating this one.
        const dl::Relation& need = db_->relation(r_pred_need_);
        while (pred_need_seen_ < need.size()) {
            auto row = need.row(pred_need_seen_++);
            dl::Value pt = row[0], px = row[1];
            auto preds = preds_[pt];   // copy: a nested call may add predicates
            for (auto& pred : preds)
                if (pred(syms_.name(px))) { insert_(r_pred_ok_, {pt, px}); break; }
        }
    }
    return db_->relation(r_impls_).contains(q);
}

std::string TraitRules::explain(const std::string& trait, const std::string& type_name) {
    satisfies(trait, type_name);
    const dl::Value q[] = {sym_(trait), sym_(type_name)};
    if (auto d = db_->explain(r_impls_, q)) return db_->render(*d);
    return "no derivation: impls(\"" + trait + "\", \"" + type_name + "\")\n";
}

} // namespace logos::compiler
