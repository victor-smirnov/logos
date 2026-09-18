// Relations, indexes and stratified semi-naive evaluation (ADR 0028).
#include "dl_internal.hpp"

#include <algorithm>
#include <cassert>
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

Relation::Index& Relation::index_(uint32_t mask) const {
    Index* ix = nullptr;
    for (auto& p : indexes_)
        if (p->mask == mask) { ix = p.get(); break; }
    if (!ix) {
        indexes_.push_back(std::make_unique<Index>());
        ix = indexes_.back().get();
        ix->mask = mask;
    }
    for (size_t n = size(); ix->built < n; ++ix->built)
        ix->buckets[hash_masked_(row(ix->built), mask)].push_back(
            static_cast<uint32_t>(ix->built));
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
    Index& ix = index_(mask);
    auto it = ix.buckets.find(hash_key(key));
    if (it == ix.buckets.end()) return;
    for (uint32_t r : it->second) {
        auto rv = row(r);
        size_t k = 0;
        bool eq = true;
        for (uint32_t c = 0; c < arity_ && eq; ++c)
            if (mask & (1u << c)) eq = rv[c] == key[k++];
        if (eq) out.push_back(r);
    }
}

std::optional<uint32_t> Relation::find(std::span<const Value> r) const {
    if (arity_ == 0) return nullary_ ? std::optional<uint32_t>(0) : std::nullopt;
    Index& ix = index_(full_mask(arity_));
    auto it = ix.buckets.find(hash_key(r));
    if (it == ix.buckets.end()) return std::nullopt;
    for (uint32_t i : it->second)
        if (std::equal(r.begin(), r.end(), row(i).begin())) return i;
    return std::nullopt;
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

Database::Database(const Program& prog, Symbols& syms, bool provenance)
    : prog_(prog), syms_(syms), provenance_(provenance) {
    for (auto& d : prog_.relations())
        rels_.push_back(std::make_unique<Relation>(static_cast<uint32_t>(d.types.size())));
    prov_rule_.resize(rels_.size());
    prov_off_.resize(rels_.size());
    for (auto& f : prog_.facts()) insert(f.rel, f.row);
}

Database::~Database() = default;

bool Database::insert(uint32_t rel, std::span<const Value> row) {
    assert(!ran_ && "insert after run()");
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
        detail::Plan plan;
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
        const auto& steps = cr->plan.steps;
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
        delta_lit = dlit;
        delta_lo  = lo;
        delta_hi  = hi;
        vals.assign(rule->var_names.size(), 0);
        prem.assign(c.plan.pos_lits.size(), {0, 0});
        bufs.resize(std::max(bufs.size(), c.plan.steps.size()));
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

    void run() {
        const auto& rules = db.prog_.rules();
        std::vector<CompiledRule> compiled(rules.size());
        n_pos.resize(rules.size());
        for (uint32_t i = 0; i < rules.size(); ++i) {
            compiled[i].id = i;
            for (auto& l : rules[i].body) n_pos[i] += l.kind == Literal::Kind::Pos;
            std::string err;
            bool ok = detail::plan_rule(rules[i], compiled[i].plan, err);
            assert(ok && "the parser accepted a rule the planner rejects");
            (void)ok;
        }
        std::vector<int32_t> stratum_of(db.rels_.size(), -1);
        for (size_t s = 0; s < db.prog_.strata().size(); ++s)
            for (uint32_t r : db.prog_.strata()[s]) stratum_of[r] = static_cast<int32_t>(s);

        for (size_t s = 0; s < db.prog_.strata().size(); ++s) {
            const auto& scc = db.prog_.strata()[s];
            std::vector<const CompiledRule*> mine;
            for (auto& c : compiled)
                if (stratum_of[rules[c.id].head.rel] == static_cast<int32_t>(s)) mine.push_back(&c);
            if (mine.empty()) continue;

            std::vector<size_t> before(db.rels_.size());
            for (uint32_t r : scc) before[r] = db.rels_[r]->size();
            for (auto* c : mine) eval(*c, -1, 0, 0);
            flush();
            ++db.stats_.rounds;

            // Delta = rows added by the last round, per relation of this SCC.
            std::vector<std::pair<size_t, size_t>> delta(db.rels_.size(), {0, 0});
            for (uint32_t r : scc) delta[r] = {before[r], db.rels_[r]->size()};

            auto recursive = [&](const CompiledRule& c) {
                for (uint32_t li : c.plan.pos_lits)
                    if (stratum_of[rules[c.id].body[li].atom.rel] == static_cast<int32_t>(s))
                        return true;
                return false;
            };
            for (;;) {
                bool any = false;
                for (uint32_t r : scc) any = any || delta[r].first < delta[r].second;
                if (!any) break;
                for (uint32_t r : scc) before[r] = db.rels_[r]->size();
                for (auto* c : mine) {
                    if (!recursive(*c)) continue;
                    for (uint32_t li : c->plan.pos_lits) {
                        uint32_t br = rules[c->id].body[li].atom.rel;
                        if (stratum_of[br] != static_cast<int32_t>(s)) continue;
                        if (delta[br].first == delta[br].second) continue;
                        eval(*c, static_cast<int32_t>(li), delta[br].first, delta[br].second);
                    }
                }
                flush();
                ++db.stats_.rounds;
                for (uint32_t r : scc) delta[r] = {before[r], db.rels_[r]->size()};
            }
        }
    }
};

void Database::run() {
    assert(!ran_ && "run() twice");
    Impl impl(*this);
    impl.run();
    ran_ = true;
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
