// A minimal bottom-up Datalog engine for logosc (ADR 0028).
//
// Rules are text in a subset of Souffle syntax, so every rule file also runs
// under Souffle, which is the engine's test oracle. The engine has no
// dependency on the rest of the compiler: callers intern their own ids into
// `Symbols` (or use dense numbers) and fill `.input` relations before `run()`.
//
// Subset (anything else is a parse error with file and line):
//   .type Name <: symbol | number
//   .decl rel(a: T, b: T)            T in {symbol, number, a .type name}
//   .input rel, rel2                 filled by the caller before run()
//   .output rel                      read by the caller after run()
//   h(X, "c", 1) :- a(X, Z), !b(Z, _), X != Z, Y = Z.
//   fact("x", 1).
//   // line and /* block */ comments
//   #include "file.dl"               resolved through the caller's resolver
//
// Evaluation: stratified (SCCs of the predicate graph; negation inside an SCC
// is an error), semi-naive within each SCC, body order as written with every
// filter hoisted to the first point its variables are bound.
#pragma once

#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace logos::dl {

using Value = uint32_t;

enum class ColType : uint8_t { Symbol, Number };

// Interned strings. Ids are dense from 0; a Database and the Program it runs
// must share one table.
class Symbols {
public:
    Value intern(std::string_view s);
    std::optional<Value> find(std::string_view s) const;
    std::string_view name(Value v) const { return store_[v]; }
    size_t size() const { return store_.size(); }

private:
    std::deque<std::string> store_;   // deque: element addresses are stable
    std::unordered_map<std::string_view, Value> ids_;
};

struct Error {
    std::string file;
    uint32_t    line = 0;
    std::string message;
    std::string str() const;
};

// Returns the text of an `#include`d file, or nullopt when it is unknown.
using IncludeResolver =
    std::function<std::optional<std::string>(std::string_view name)>;

struct Term {
    enum class Kind : uint8_t { Var, Const, Wild };
    Kind     kind  = Kind::Wild;
    uint32_t value = 0;   // variable index for Var, the constant for Const
};

struct Atom {
    uint32_t          rel = 0;
    std::vector<Term> args;
};

struct Literal {
    enum class Kind : uint8_t { Pos, Neg, Eq, Ne };
    Kind kind = Kind::Pos;
    Atom atom;            // Pos, Neg
    Term lhs, rhs;        // Eq, Ne
};

struct Rule {
    Atom                     head;
    std::vector<Literal>     body;
    std::vector<std::string> var_names;   // index = variable id
    std::string              file;
    uint32_t                 line = 0;
    std::string              text;        // source text, for explanations
};

struct RelDecl {
    std::string              name;
    std::vector<std::string> attr_names;
    std::vector<ColType>     types;
    bool                     input  = false;
    bool                     output = false;
    std::string              file;
    uint32_t                 line = 0;
};

class Program {
public:
    struct Fact {
        uint32_t           rel = 0;
        std::vector<Value> row;
    };

    // Parses, type-checks and stratifies. Constants are interned into `syms`.
    static std::expected<Program, Error>
    parse(std::string_view text, std::string_view file, Symbols& syms,
          const IncludeResolver& resolver = {});

    const std::vector<RelDecl>& relations() const { return rels_; }
    std::optional<uint32_t> relation(std::string_view name) const;
    const std::vector<Rule>& rules() const { return rules_; }
    const std::vector<Fact>& facts() const { return facts_; }
    // Relation ids grouped into SCCs, dependencies first.
    const std::vector<std::vector<uint32_t>>& strata() const { return strata_; }
    // A program without negation is monotone: adding input rows after run()
    // and running again is sound, so a Database may be driven incrementally.
    bool has_negation() const { return has_negation_; }

private:
    friend class Parser;
    std::vector<RelDecl>                      rels_;
    std::unordered_map<std::string, uint32_t> rel_ids_;
    std::vector<Rule>                         rules_;
    std::vector<Fact>                         facts_;
    std::vector<std::vector<uint32_t>>        strata_;
    bool                                      has_negation_ = false;
};

// An append-only set of fixed-arity rows. Row ids are dense and stable.
class Relation {
public:
    explicit Relation(uint32_t arity) : arity_(arity) {}

    uint32_t arity() const { return arity_; }
    size_t   size() const { return arity_ ? data_.size() / arity_ : nullary_; }
    std::span<const Value> row(size_t i) const {
        return {data_.data() + i * arity_, arity_};
    }
    bool contains(std::span<const Value> row) const;
    // Row id of an equal row, or nullopt.
    std::optional<uint32_t> find(std::span<const Value> row) const;
    // Appends when absent. Returns the row id when appended, nullopt when
    // the row was already present.
    std::optional<uint32_t> insert(std::span<const Value> row);

    // Row ids whose columns in `mask` equal `key` (key holds the values of
    // the masked columns, in column order). Candidates are exact matches.
    void lookup(uint32_t mask, std::span<const Value> key,
                std::vector<uint32_t>& out) const;

private:
    struct Index {
        uint32_t mask  = 0;
        size_t   built = 0;
        std::unordered_map<uint64_t, std::vector<uint32_t>> buckets;
    };
    Index& index_(uint32_t mask) const;
    uint64_t hash_masked_(std::span<const Value> row, uint32_t mask) const;

    uint32_t                                    arity_;
    std::vector<Value>                          data_;
    size_t                                      nullary_ = 0;  // 0 or 1
    mutable std::vector<std::unique_ptr<Index>> indexes_;
};

class Database {
public:
    // Inline facts of `prog` are inserted here. `prog` and `syms` must
    // outlive the Database.
    Database(const Program& prog, Symbols& syms, bool provenance = true);
    ~Database();

    const Program& program() const { return prog_; }
    Symbols&       symbols() { return syms_; }

    // Adds an input row (the relation need not be declared .input; Souffle
    // semantics only restrict where facts come from, not what may hold them).
    // After run(), only a program without negation accepts more rows.
    bool insert(uint32_t rel, std::span<const Value> row);
    // Evaluates to fixpoint. May be called again after more insert()s when
    // the program has no negation; it then continues from the new rows.
    void run();

    const Relation& relation(uint32_t rel) const { return *rels_[rel]; }
    std::string format_row(uint32_t rel, std::span<const Value> row) const;

    // The first derivation of a row. `rule < 0` marks an input or inline
    // fact. Premises are the rows matched by the rule's positive body atoms,
    // in body order. `max_nodes` bounds the tree (derivations share rows).
    struct Derivation {
        uint32_t                rel  = 0;
        uint32_t                row  = 0;
        int32_t                 rule = -1;
        std::vector<Derivation> premises;
        bool                    truncated = false;
    };
    std::optional<Derivation> explain(uint32_t rel, std::span<const Value> row,
                                      size_t max_nodes = 256) const;
    std::string render(const Derivation& d, int indent = 0) const;

    struct Stats {
        size_t rounds  = 0;
        size_t derived = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    struct Impl;
    const Program&                         prog_;
    Symbols&                               syms_;
    bool                                   provenance_;
    std::vector<std::unique_ptr<Relation>> rels_;
    // Provenance, parallel to each relation's rows.
    std::vector<std::vector<int32_t>>      prov_rule_;
    std::vector<std::vector<uint32_t>>     prov_off_;
    std::vector<std::pair<uint32_t, uint32_t>> prov_pool_;   // (rel, row)
    Stats                                  stats_;
    bool                                   ran_ = false;
    std::unique_ptr<Impl>                  impl_;
    friend struct Impl;
};

} // namespace logos::dl
