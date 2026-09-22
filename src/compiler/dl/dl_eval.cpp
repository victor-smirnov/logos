// Relations, indexes and stratified semi-naive evaluation (ADR 0028).
#include "dl_internal.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <functional>

namespace logos::dl {

// ── Relation ────────────────────────────────────────────────────────────

namespace {

uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h ^= h >> 31;
    h *= 0xbf58476d1ce4e5b9ULL;
    return h;
}

uint64_t hash_key(std::span<const Value> key) {
    uint64_t h = 0x84222325cbf29ce4ULL;
    for (Value v : key) h = mix(h, v);
    return h;
}

uint32_t full_mask(uint32_t arity) {
    return arity >= 32 ? ~0u : ((1u << arity) - 1);
}

} // namespace

uint64_t Relation::hash_masked_(std::span<const Value> row, uint32_t mask) const {
    uint64_t h = 0x84222325cbf29ce4ULL;
    for (uint32_t c = 0; c < arity_; ++c)
        if (mask & (1u << c)) h = mix(h, row[c]);
    return h;
}

bool Relation::eq_masked_(std::span<const Value> rv, uint32_t mask,
                          std::span<const Value> key) const {
    size_t k = 0;
    for (uint32_t c = 0; c < arity_; ++c)
        if ((mask & (1u << c)) && rv[c] != key[k++]) return false;
    return true;
}

Relation::Index& Relation::index_(uint32_t mask) const {
    Index* ix = nullptr;
    for (auto& p : indexes_)
        if (p->mask == mask) { ix = p.get(); break; }
    if (!ix) {
        indexes_.push_back(std::make_unique<Index>());
        ix = indexes_.back().get();
        ix->mask = mask;
    }
    const size_t n = size();
    if (ix->built == n) return *ix;
    // Keep the load factor at most 1/2; growing rebuilds the chains.
    size_t cap = ix->heads.size();
    if (cap < 2 * n) {
        size_t want = std::max<size_t>(16, cap);
        while (want < 2 * n) want *= 2;
        ix->heads.assign(want, ~0u);
        ix->built = 0;
    }
    ix->next.resize(n);
    const size_t m = ix->heads.size() - 1;
    for (; ix->built < n; ++ix->built) {
        uint32_t& head = ix->heads[hash_masked_(row(ix->built), mask) & m];
        ix->next[ix->built] = head;
        head = static_cast<uint32_t>(ix->built);
    }
    return *ix;
}

void Relation::lookup(uint32_t mask, std::span<const Value> key,
                      std::vector<uint32_t>& out) const {
    out.clear();
    const size_t n = size();
    if (mask == 0) {
        for (size_t i = 0; i < n; ++i) out.push_back(static_cast<uint32_t>(i));
        return;
    }
    if (n == 0) return;
    Index& ix = index_(mask);
    // Chains are newest-first; callers see rows in insertion order.
    for (uint32_t r = ix.heads[hash_key(key) & (ix.heads.size() - 1)]; r != ~0u; r = ix.next[r])
        if (eq_masked_(row(r), mask, key)) out.push_back(r);
    std::reverse(out.begin(), out.end());
}

std::optional<uint32_t> Relation::find(std::span<const Value> r) const {
    if (arity_ == 0) return nullary_ ? std::optional<uint32_t>(0) : std::nullopt;
    if (size() == 0) return std::nullopt;
    const uint32_t full = full_mask(arity_);
    Index& ix = index_(full);
    for (uint32_t i = ix.heads[hash_key(r) & (ix.heads.size() - 1)]; i != ~0u; i = ix.next[i])
        if (std::equal(r.begin(), r.end(), row(i).begin())) return i;
    return std::nullopt;
}

void Relation::clear() {
    data_.clear();
    nullary_ = 0;
    for (auto& ix : indexes_) {
        // The head array keeps the size the LARGEST use gave it; filling all of
        // it on every clear made each later (small) body pay for one big one.
        // A large array is shrunk back (index_ regrows it on demand), so a
        // clear costs what the relation held, not what it once held.
        if (ix->heads.size() > 1024) ix->heads.assign(16, ~0u);
        else std::fill(ix->heads.begin(), ix->heads.end(), ~0u);
        ix->next.clear();
        ix->built = 0;
    }
}

bool Relation::contains(std::span<const Value> r) const { return find(r).has_value(); }

std::optional<uint32_t> Relation::insert(std::span<const Value> r) {
    assert(r.size() == arity_);
    if (find(r)) return std::nullopt;
    if (arity_ == 0) { nullary_ = 1; return 0; }
    data_.insert(data_.end(), r.begin(), r.end());
    return static_cast<uint32_t>(size() - 1);
}

// ── Database ────────────────────────────────────────────────────────────

// Adding rows after run() can retract a conclusion drawn from a negation, which
// an append-only evaluator cannot do. Refused in every build type, not asserted.
static void require_monotone(const Program& p, const char* what) {
    if (!p.has_negation()) return;
    std::fprintf(stderr, "logos::dl: %s after run() on a program with negation\n", what);
    std::abort();
}

bool Database::insert(uint32_t rel, std::span<const Value> row) {
    if (ran_) require_monotone(prog_, "insert()");
    auto id = rels_[rel]->insert(row);
    if (!id) return false;
    if (provenance_) {
        prov_rule_[rel].push_back(-1);
        prov_off_[rel].push_back(static_cast<uint32_t>(prov_pool_.size()));
    }
    return true;
}

struct Database::Impl {
    explicit Impl(Database& d) : db(d) {}
    Database& db;

    struct CompiledRule {
        uint32_t     id = 0;
        detail::Plan plan;                     // no delta: body order as written
        // One plan per positive literal, parallel to `plan.pos_lits`, each
        // driven FROM that literal so the delta rows bind before anything else
        // is looked at.
        std::vector<detail::Plan> delta_plans;
    };
    // One derived row waiting for the end of the round.
    struct Pending {
        uint32_t rel;
        int32_t  rule;
        uint32_t row_off;
        uint32_t prem_off;
    };
    std::vector<Pending>                        pending;
    std::vector<Value>                          pending_vals;
    std::vector<std::pair<uint32_t, uint32_t>>  pending_prem;

    // Per-evaluation state.
    const Rule*                                 rule  = nullptr;
    const CompiledRule*                         cr    = nullptr;
    const detail::Plan*                         pl    = nullptr;
    int32_t                                     delta_lit = -1;
    size_t                                      delta_lo = 0, delta_hi = 0;
    std::vector<Value>                          vals;
    std::vector<std::pair<uint32_t, uint32_t>>  prem;     // per positive atom
    std::vector<std::vector<uint32_t>>          bufs;     // per step
    std::vector<Value>                          key;
    std::vector<size_t>                         n_pos;    // positive atoms per rule

    Value value_of(const Term& t) const {
        return t.kind == Term::Kind::Const ? t.value : vals[t.value];
    }

    void build_key(const detail::Step& s) {
        key.clear();
        for (auto& op : s.cols)
            if (op.kind == detail::ColOp::Kind::Key) key.push_back(value_of(op.term));
    }

    // Binds the row's Bind columns and checks its Repeat columns.
    bool accept_row(const detail::Step& s, std::span<const Value> rv) {
        for (auto& op : s.cols) {
            if (op.kind == detail::ColOp::Kind::Bind) vals[op.term.value] = rv[op.col];
            else if (op.kind == detail::ColOp::Kind::Repeat && vals[op.term.value] != rv[op.col])
                return false;
        }
        return true;
    }

    void step(size_t i, uint32_t pos_idx) {
        const auto& steps = pl->steps;
        if (i == steps.size()) { emit(); return; }
        const detail::Step& s = steps[i];
        switch (s.kind) {
            case detail::Step::Kind::Scan: {
                const Atom& a = rule->body[s.lit].atom;
                const Relation& R = *db.rels_[a.rel];
                build_key(s);
                auto& buf = bufs[i];
                if (static_cast<int32_t>(s.lit) == delta_lit) {
                    buf.clear();
                    for (size_t r = delta_lo; r < delta_hi; ++r) {
                        auto rv = R.row(r);
                        size_t k = 0;
                        bool eq = true;
                        for (auto& op : s.cols)
                            if (op.kind == detail::ColOp::Kind::Key)
                                eq = eq && rv[op.col] == key[k++];
                        if (eq) buf.push_back(static_cast<uint32_t>(r));
                    }
                } else {
                    R.lookup(s.mask, key, buf);
                }
                // `buf` is not touched by deeper steps (each step owns its own).
                for (uint32_t r : buf) {
                    if (!accept_row(s, R.row(r))) continue;
                    prem[pos_idx] = {a.rel, r};
                    step(i + 1, pos_idx + 1);
                }
                return;
            }
            case detail::Step::Kind::Neg: {
                const Atom& a = rule->body[s.lit].atom;
                build_key(s);
                db.rels_[a.rel]->lookup(s.mask, key, bufs[i]);
                if (bufs[i].empty()) step(i + 1, pos_idx);
                return;
            }
            case detail::Step::Kind::Cmp: {
                const Literal& l = rule->body[s.lit];
                bool eq = value_of(l.lhs) == value_of(l.rhs);
                if (eq == (l.kind == Literal::Kind::Eq)) step(i + 1, pos_idx);
                return;
            }
            case detail::Step::Kind::Bind:
                vals[s.var] = value_of(s.from);
                step(i + 1, pos_idx);
                return;
        }
    }

    void emit() {
        Pending p;
        p.rel      = rule->head.rel;
        p.rule     = static_cast<int32_t>(cr->id);
        p.row_off  = static_cast<uint32_t>(pending_vals.size());
        p.prem_off = static_cast<uint32_t>(pending_prem.size());
        for (auto& t : rule->head.args) pending_vals.push_back(value_of(t));
        if (db.provenance_)
            pending_prem.insert(pending_prem.end(), prem.begin(), prem.end());
        pending.push_back(p);
    }

    void eval(const CompiledRule& c, int32_t dlit, size_t lo, size_t hi) {
        rule = &db.prog_.rules()[c.id];
        cr   = &c;
        pl   = &c.plan;
        if (dlit >= 0)
            for (size_t k = 0; k < c.plan.pos_lits.size(); ++k)
                if (c.plan.pos_lits[k] == static_cast<uint32_t>(dlit) &&
                    k < c.delta_plans.size()) { pl = &c.delta_plans[k]; break; }
        delta_lit = dlit;
        delta_lo  = lo;
        delta_hi  = hi;
        vals.assign(rule->var_names.size(), 0);
        prem.assign(pl->pos_lits.size(), {0, 0});
        bufs.resize(std::max(bufs.size(), pl->steps.size()));
        step(0, 0);
    }

    // Moves pending rows into their relations; returns rows added.
    size_t flush() {
        size_t added = 0;
        for (auto& p : pending) {
            Relation& R = *db.rels_[p.rel];
            std::span<const Value> row(pending_vals.data() + p.row_off, R.arity());
            auto id = R.insert(row);
            if (!id) continue;
            ++added;
            if (db.provenance_) {
                db.prov_rule_[p.rel].push_back(p.rule);
                db.prov_off_[p.rel].push_back(static_cast<uint32_t>(db.prov_pool_.size()));
                size_t np = n_pos[p.rule];
                db.prov_pool_.insert(db.prov_pool_.end(),
                                     pending_prem.begin() + p.prem_off,
                                     pending_prem.begin() + p.prem_off + np);
            }
        }
        db.stats_.derived += added;
        pending.clear();
        pending_vals.clear();
        pending_prem.clear();
        return added;
    }

    // Built once per Database: plans, stratum of each relation, rules per
    // stratum, and per stratum the relation sizes it has already consumed.
    std::vector<CompiledRule>                   compiled;
    std::vector<int32_t>                        stratum_of;
    std::vector<std::vector<const CompiledRule*>> rules_of;
    std::vector<std::vector<size_t>>            consumed;   // [stratum][rel]
    std::vector<bool>                           evaluated;  // [stratum]

    void setup() {
        const auto& rules = db.prog_.rules();
        compiled.resize(rules.size());
        n_pos.resize(rules.size());
        for (uint32_t i = 0; i < rules.size(); ++i) {
            compiled[i].id = i;
            for (auto& l : rules[i].body) n_pos[i] += l.kind == Literal::Kind::Pos;
            std::string err;
            bool ok = detail::plan_rule(rules[i], compiled[i].plan, err);
            assert(ok && "the parser accepted a rule the planner rejects");
            (void)ok;
            compiled[i].delta_plans.resize(compiled[i].plan.pos_lits.size());
            for (size_t k = 0; k < compiled[i].plan.pos_lits.size(); ++k) {
                std::string derr;
                bool dok = detail::plan_rule(rules[i], compiled[i].delta_plans[k], derr,
                                             static_cast<int32_t>(compiled[i].plan.pos_lits[k]));
                assert(dok && "a delta-driven order of an accepted rule must plan");
                (void)dok;
            }
        }
        const size_t ns = db.prog_.strata().size();
        stratum_of.assign(db.rels_.size(), -1);
        for (size_t s = 0; s < ns; ++s)
            for (uint32_t r : db.prog_.strata()[s]) stratum_of[r] = static_cast<int32_t>(s);
        rules_of.assign(ns, {});
        for (auto& c : compiled) rules_of[stratum_of[rules[c.id].head.rel]].push_back(&c);
        consumed.assign(ns, std::vector<size_t>(db.rels_.size(), 0));
        evaluated.assign(ns, false);
    }

    // The first call evaluates every stratum from scratch. A later call (only
    // for a program without negation, where adding rows never retracts one)
    // continues semi-naively from the rows each stratum has not consumed yet.
    void run() {
        const auto& rules = db.prog_.rules();
        for (size_t s = 0; s < db.prog_.strata().size(); ++s) {
            const auto& scc  = db.prog_.strata()[s];
            const auto& mine = rules_of[s];
            if (mine.empty()) continue;
            const int32_t si = static_cast<int32_t>(s);
            auto& seen = consumed[s];

            std::vector<size_t> before(db.rels_.size());
            for (uint32_t r : scc) before[r] = db.rels_[r]->size();
            if (!evaluated[s]) {
                for (auto* c : mine) eval(*c, -1, 0, 0);
                evaluated[s] = true;
            } else {
                // Every new derivation uses at least one row this stratum has
                // not consumed; take each such atom in turn as the delta.
                std::vector<size_t> now(db.rels_.size());
                for (size_t r = 0; r < now.size(); ++r) now[r] = db.rels_[r]->size();
                for (auto* c : mine)
                    for (uint32_t li : c->plan.pos_lits) {
                        uint32_t br = rules[c->id].body[li].atom.rel;
                        if (seen[br] < now[br])
                            eval(*c, static_cast<int32_t>(li), seen[br], now[br]);
                    }
            }
            flush();
            ++db.stats_.rounds;

            // Delta = rows added by the last round, per relation of this SCC.
            std::vector<std::pair<size_t, size_t>> delta(db.rels_.size(), {0, 0});
            for (uint32_t r : scc) delta[r] = {before[r], db.rels_[r]->size()};

            for (;;) {
                bool any = false;
                for (uint32_t r : scc) any = any || delta[r].first < delta[r].second;
                if (!any) break;
                for (uint32_t r : scc) before[r] = db.rels_[r]->size();
                for (auto* c : mine)
                    for (uint32_t li : c->plan.pos_lits) {
                        uint32_t br = rules[c->id].body[li].atom.rel;
                        if (stratum_of[br] != si) continue;
                        if (delta[br].first == delta[br].second) continue;
                        eval(*c, static_cast<int32_t>(li), delta[br].first, delta[br].second);
                    }
                flush();
                ++db.stats_.rounds;
                for (uint32_t r : scc) delta[r] = {before[r], db.rels_[r]->size()};
            }
            for (size_t r = 0; r < seen.size(); ++r) seen[r] = db.rels_[r]->size();
        }
    }
};

Database::Database(const Program& prog, Symbols& syms, bool provenance)
    : prog_(prog), syms_(syms), provenance_(provenance) {
    for (auto& d : prog_.relations())
        rels_.push_back(std::make_unique<Relation>(static_cast<uint32_t>(d.types.size())));
    prov_rule_.resize(rels_.size());
    prov_off_.resize(rels_.size());
    for (auto& f : prog_.facts()) insert(f.rel, f.row);
}

Database::~Database() = default;

void Database::run() {
    if (ran_) require_monotone(prog_, "run()");
    if (!impl_) {
        impl_ = std::make_unique<Impl>(*this);
        impl_->setup();
    }
    impl_->run();
    ran_ = true;
}

void Database::clear(bool provenance) {
    provenance_ = provenance;
    for (auto& r : rels_) r->clear();
    for (auto& v : prov_rule_) v.clear();
    for (auto& v : prov_off_) v.clear();
    prov_pool_.clear();
    stats_ = {};
    ran_ = false;
    if (impl_) {
        for (auto& c : impl_->consumed) std::fill(c.begin(), c.end(), 0);
        std::fill(impl_->evaluated.begin(), impl_->evaluated.end(), false);
    }
    for (auto& f : prog_.facts()) insert(f.rel, f.row);
}

std::string Database::format_row(uint32_t rel, std::span<const Value> row) const {
    const RelDecl& d = prog_.relations()[rel];
    std::string s = d.name + "(";
    for (size_t i = 0; i < row.size(); ++i) {
        if (i) s += ", ";
        if (d.types[i] == ColType::Symbol) s += std::format("\"{}\"", syms_.name(row[i]));
        else s += std::to_string(static_cast<int32_t>(row[i]));
    }
    return s + ")";
}

std::optional<Database::Derivation>
Database::explain(uint32_t rel, std::span<const Value> row, size_t max_nodes) const {
    auto id = rels_[rel]->find(row);
    if (!id) return std::nullopt;
    size_t budget = max_nodes;
    std::function<Derivation(uint32_t, uint32_t)> build = [&](uint32_t r, uint32_t i) {
        Derivation d;
        d.rel = r;
        d.row = i;
        if (!provenance_) { d.truncated = true; return d; }
        d.rule = prov_rule_[r][i];
        if (d.rule < 0) return d;
        if (budget == 0) { d.truncated = true; return d; }
        --budget;
        size_t np = 0;
        for (auto& l : prog_.rules()[d.rule].body) np += l.kind == Literal::Kind::Pos;
        uint32_t off = prov_off_[r][i];
        for (size_t k = 0; k < np; ++k) {
            auto [pr, pi] = prov_pool_[off + k];
            d.premises.push_back(build(pr, pi));
        }
        return d;
    };
    return build(rel, *id);
}

std::string Database::render(const Derivation& d, int indent) const {
    std::string pad(static_cast<size_t>(indent) * 2, ' ');
    std::string s = pad + format_row(d.rel, rels_[d.rel]->row(d.row));
    if (d.rule < 0) s += "  [fact]";
    else {
        const Rule& r = prog_.rules()[d.rule];
        s += std::format("  [{}:{}]", r.file, r.line);
    }
    if (d.truncated) s += "  ...";
    s += "\n";
    for (auto& p : d.premises) s += render(p, indent + 1);
    return s;
}

} // namespace logos::dl
