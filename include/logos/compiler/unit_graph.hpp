// unit_graph — THE partition of a module's lowered functions into compile
// UNITS, and THE order in which those units may be built.
//
// One artifact, two consumers:
//
//   * the metaprog dispatch loop, which needs to know that a metafunction's
//     PROVIDER unit must be object-compiled before the JIT can call it
//     (§2 of the design: a cross-unit edge means AOT-then-load, a same-SCC
//     edge means genuine JIT compilation — the bootstrap cycle);
//   * the backend, which emits one object per unit and can do so in parallel
//     within a level (§4).
//
// Both call levels(). There is deliberately no second order accessor and no
// public constructor other than build(): the recurring defect in this codebase
// is two consumers deriving "the same" fact their own way and drifting. The
// order is computed once, and the driver RECORDS the order it actually used
// into the sidecar so a gate can compare it against flatten(levels()).
//
// ── What a node is ─────────────────────────────────────────────────────────
// The partition is TOTAL over lowered functions: every function the backend
// would emit belongs to exactly one unit. Totality is what makes this an
// artifact rather than a heuristic, and it is what a gate can assert.
//
//   Source     one real .logos file of the module.
//   Generated  one metaprogram-emitted FAMILY (not one chunk — a family is
//              3-4 chunks and the family is the independence boundary).
//   Common     the residue: shared generic instances two families both
//              demand, join functions, main, everything unattributed. A real
//              node with real cost, scheduled like any other.
//
// ── Where ownership comes from ─────────────────────────────────────────────
// DECLARED, not recovered. Each AST carries a unit key (SemaOptions::
// ast_unit_key, pushed by the emitter — see logos_emit_item_blob_subst_in),
// sema stamps it onto every function it lowers (decl_keys::UNIT_KEY), and
// mono deliberately does NOT copy it. So:
//
//   UNIT_KEY non-empty  →  an emitter or a source file declared this owner
//   UNIT_KEY empty      →  a mono clone / dependency body, owner DERIVED
//
// The family hash tag that appears in mangled link names (`Hs<16hex>` and
// `hs_<16hex>`) is a CANARY over this, never the mechanism: under
// LOGOS_VERIFY_UNITS=1 the two derivations are checked against each other.
// Recovering the partition by parsing symbols would be one afternoon's work
// and would bake in a 32% undercount the moment one of the two spellings was
// missed.
//
// ── HOW BIG THE POPULATION ACTUALLY IS (measured, 2026-08-03) ──────────────
// The ordering work buys ONE edge in the entire stdlib, not three. A survey
// that stripped COMMENTS before counting `name!(` still counted two hits
// inside STRING LITERALS — error-message text that mentions `deem!(…)` and
// `wql!:` — and reported plan_walker.logos and rexpr_walk.logos as consumers
// of mem/wql/wql.logos. Stripping comments AND double-quoted strings over the
// 226 stdlib files leaves:
//     cross-TU (case 1):  trama_selfuse.logos -> trama_render.logos   [1]
//     same-TU  (case 2):  std/fmt/fmt.logos uses its own format!      [1]
// (`quote_item!`/`quote_expr!`/`if!` are builtins, not metafunction calls.)
// This does not change the design — a rule that is right for one edge is the
// same rule for a thousand — but it does change its SIZING: the justification
// rests on user-shaped code (tests/logos/pass alone: 334 deem, 337 name!(, 31
// derive, 13 container), never on the library.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <logos/compiler/lir.hpp>
#include <logos/compiler/str_map.hpp>

namespace logos::compiler {

// The literal key of the Common unit. Sema stamps this rather than "" so that
// an EMPTY UNIT_KEY on a function has exactly one meaning (§1.2).
inline constexpr std::string_view kCommonKey = "<common>";

// THE §1.1 kind rule, in ONE place. Sema calls it to stamp functions and
// UnitGraph::build calls it to create nodes; if each spelled the rule itself,
// a function could be stamped into a unit the graph never created — the exact
// drift this file exists to prevent.
//
//   emitter-declared key → that key   (Generated)
//   ordinary source file → its path   (Source)
//   neither              → <common>   (Common)
//
// A leading '<' marks a synthesised placeholder name (`<metaprog>`,
// `<metaprog-blob>`, …) — those name no unit of their own.
inline std::string unit_key_for_ast(const std::vector<std::string>& filenames,
                                    const std::vector<std::string>& ast_unit_key,
                                    size_t ai) {
    if (ai < ast_unit_key.size() && !ast_unit_key[ai].empty()) return ast_unit_key[ai];
    if (ai < filenames.size()) {
        const std::string& f = filenames[ai];
        if (!f.empty() && f.front() != '<') return f;
    }
    return std::string(kCommonKey);
}

struct UnitId {
    enum class Kind : uint8_t { Common, Source, Generated };
    Kind        kind = Kind::Common;
    std::string key;   // Common:    "<common>" — exactly one per module
                       // Source:    canonical .logos path
                       // Generated: emitter-declared key, e.g. "ctr:Hs6b2e7f08e6a89698"
};

// "The compiler process must be able to CALL a function of `to` before it
// finishes `from`." That is the ONLY relation that constrains order, and it
// comes from exactly two already-recorded tables. Ordinary inter-unit calls
// are link-time and impose no order — making them edges would turn this into
// the whole call graph, in which every unit depends on every other.
struct UnitEdge {
    uint32_t from = 0, to = 0;         // consumer, provider
    uint32_t site_ast = 0, site_off = 0;  // provenance: an edge can say WHY
    enum class Cause : uint8_t { Metacall, Trigger } cause = Cause::Metacall;
};

// ── WHY THE EDGE SOURCE HAS TO BE ACCUMULATED ──────────────────────────────
// prog.metacall_sites is not a record of the module's metacalls. It is a
// WORKLIST: the dispatch loop drains it, splicing each site's emitted items
// into `asts`, and the next sema over the rewritten asts finds nothing left to
// dispatch. The FINAL post-sema program therefore has metacall_sites == 0 for
// every module that compiled successfully — including one whose whole point is
// a cross-TU metafunction call. Building the graph from that program does not
// find "no dependencies"; it looks at an emptied worklist.
//
// Measured, before this type existed: a genuine case-1 module (a provider TU
// defining `#[fn_macro] emit_gamma`, a consumer TU calling `emit_gamma!{}`)
// reported units=3 edges=0 levels=1 — "three independent units, build them in
// any order, in parallel". That is not a missing edge, it is a FALSE claim of
// independence, and nothing in the artifact distinguished it from a module
// that really has no ordering constraint.
//
// So the facts are collected WHERE THEY EXIST — once per dispatch round, as
// the round's sema program is produced — and the graph is built from the
// union. `rounds_` is what makes the difference observable: zero rounds noted
// means nothing was ever consulted, and UnitGraph refuses to claim an order.
struct UnitOrderFact {
    size_t          consumer_ast = 0;
    uint32_t        site_off = 0;
    std::string     provider_file;   // EMPTY = the provider's file is unknown
    std::string     callee;          // provenance, for the diagnostic
    UnitEdge::Cause cause = UnitEdge::Cause::Metacall;
};

class UnitOrderFacts {
public:
    // One dispatch round. Call it on EVERY program the driver lowers, including
    // the final one — a site that survives to the end is as real as one that
    // was drained, and `note` deduplicates.
    void note_program(const lir::LProgram& prog);

    const std::vector<UnitOrderFact>& facts() const { return facts_; }
    size_t rounds() const { return rounds_; }
    // A round in which the accumulator was never called is indistinguishable
    // from a module with no metacalls UNLESS this is recorded. It is the whole
    // reason the type exists; see UnitGraph::order_established().
    bool consulted() const { return rounds_ > 0; }

private:
    void note(UnitOrderFact f);
    std::vector<UnitOrderFact> facts_;
    size_t rounds_ = 0;
};

class UnitGraph {
public:
    static constexpr uint32_t kCommon = 0;   // unit 0 is always Common
    static constexpr uint32_t kNone   = UINT32_MAX;

    // Phase A — nodes, from the post-sema program; ORDER edges, from the facts
    // the driver accumulated across every dispatch round (see UnitOrderFacts
    // for why the final program cannot be the source). `facts` is REQUIRED and
    // by reference: an optional edge source is one a caller can forget, and a
    // caller that forgets it gets an all-independent graph that looks correct.
    static UnitGraph build(const lir::LProgram&            prog,
                           const std::vector<std::string>& filenames,
                           const std::vector<std::string>& ast_unit_key,
                           const std::vector<bool>&        from_binary,
                           const UnitOrderFacts&           facts);

    // Phase B — fn → unit ownership, from the post-mono program. Same object;
    // a second FILL, not a second graph. Runs the §1.3 rule:
    //
    //   A function is owned by unit U iff every unit that references it is U.
    //   Otherwise it is owned by Common.
    //
    // A pure function of the LIR: it cannot depend on the schedule.
    void assign_ownership(const lir::LProgram& post_mono);

    // A schedulable WORK ITEM: one SCC of the unit graph. Units inside an SCC
    // need each other's machine code, so they are NOT independent and cannot be
    // handed to different workers — they are case (2), the bootstrap cycle,
    // JIT-compiled together. Making the group (not the unit) the element of a
    // level is what makes "a level is a mutually independent set" TRUE rather
    // than nearly true: with units as elements, the two halves of a real
    // bootstrap cycle sat side by side in level 0, which reads as "build these
    // two concurrently, in any order" — the precise opposite of what a cycle
    // means. `units.size() > 1` ⟺ bootstrap cycle, so a consumer never has to
    // re-derive that from scc ids.
    struct UnitGroup {
        uint32_t              scc = 0;
        std::vector<uint32_t> units;   // ascending; size 1 in the common case
    };

    // THE ORDER. Condensed-SCC topological levels; a level is a mutually
    // independent set OF GROUPS. Both the sequential and the parallel driver
    // call THIS — they are the same loop, with the inner `for` handed to a pool.
    //
    // SAFE BY CONSTRUCTION IN BOTH DIRECTIONS. A level wider than one group is
    // a POSITIVE claim ("these may be built concurrently"), so it is only ever
    // made from a COMPLETE edge set. When the edge set is not known complete —
    // no round was consulted, or some site's provider file was unresolved —
    // compute_levels() emits the TOTAL order instead: one unit per level, in
    // unit-index order. That is always sound (it is the sequential build) and
    // it claims no parallelism the evidence does not support. The degradation
    // is recorded, printed and written to the sidecar; it is never silent.
    const std::vector<std::vector<UnitGroup>>& levels() const { return levels_; }

    // Whether levels() is a DERIVED partial order (true) or the safe total
    // order a missing/incomplete edge source forces (false). A consumer that
    // parallelises must read this; the sidecar and --stats both print it.
    bool   order_established() const { return order_established_; }
    size_t order_facts() const { return order_facts_; }
    size_t order_rounds() const { return order_rounds_; }
    // Sites whose provider file sema could not name. Each one is a potential
    // edge that is NOT in edges_ — which is exactly why >0 forces the total
    // order rather than being reported and ignored.
    size_t unresolved_providers() const { return unresolved_providers_; }
    // Sites resolved to a file that is not a source of THIS module: the
    // provider is already object code in a dependency archive, so it imposes
    // no intra-module order. Resolved, not unknown — counted apart so the two
    // can never be confused.
    size_t external_providers() const { return external_providers_; }

    const std::vector<UnitId>&   units() const { return units_; }
    const std::vector<UnitEdge>& edges() const { return edges_; }

    uint32_t scc_of(uint32_t unit) const {
        return unit < scc_.size() ? scc_[unit] : 0;
    }
    // Total over every post-mono function once assign_ownership has run.
    // Unknown names answer kCommon rather than a sentinel: an unpartitioned
    // function must still be EMITTED somewhere, and Common is always emitted.
    uint32_t owner_of(std::string_view link_name) const {
        auto it = owner_.find(std::string(link_name));
        return it == owner_.end() ? kCommon : it->second;
    }
    bool has_owner(std::string_view link_name) const {
        return owner_.count(std::string(link_name)) != 0;
    }

    // The unit an AST index belongs to (kNone for a dependency AST).
    uint32_t unit_of_ast(size_t ast_idx) const {
        return ast_idx < ast_unit_.size() ? ast_unit_[ast_idx] : kNone;
    }
    uint32_t find_unit(std::string_view key) const {
        auto it = by_key_.find(std::string(key));
        return it == by_key_.end() ? kNone : it->second;
    }

    // Counts for --stats and for the gate. `owned_non_common` is the fraction
    // the design predicts (74% for mixed_concrete_generic, 58% for ctr_vector).
    struct Census {
        // max_level_width counts GROUPS, not units — it is the width the
        // schedule can actually use. `bootstrap_cycles` is the number of
        // groups holding more than one unit: case (2), compiled together.
        size_t units = 0, edges = 0, levels = 0, max_level_width = 0;
        size_t bootstrap_cycles = 0;
        size_t fns_total = 0;
        // A dependency body is in Common only because it belongs to ANOTHER
        // module; counting it against this module's partition would report a
        // 3% attribution rate for a module that attributed everything it owns.
        // The honest denominator is fns_total - fns_dependency.
        size_t fns_dependency = 0;
        size_t fns_declared = 0;   // an emitter or a source file named the owner
        size_t fns_derived = 0;    // owner came from the referrer rule (§1.3)
        size_t fns_unreferenced = 0; // undeclared AND never referenced → Common
        size_t fns_non_common = 0;
        size_t largest_unit_fns = 0;
        // Reach of the §1.3 derivation walk — see assign_ownership.
        size_t bodies_walked = 0, callee_hits = 0, callee_misses = 0;
        // Provenance of the ORDER — see UnitOrderFacts. `order_established`
        // false means levels() is the safe total order, not a derived one.
        bool   order_established = false;
        size_t order_rounds = 0, order_facts = 0;
        size_t unresolved_providers = 0, external_providers = 0;
        size_t fns_module() const { return fns_total - fns_dependency; }

        // ── THE LOAD-BEARING NUMBER ────────────────────────────────────────
        // `fns_non_common` says how much of the module the partition ATTRIBUTED.
        // It is not what a parallel backend can do with it, and reading it as
        // if it were is the arithmetic error this field exists to make
        // impossible: attributing 24% of a module whose remaining 76% sits in
        // ONE unit buys 1.27x, not 1.3x-per-worker.
        //
        // Amdahl over the partition, at infinite width: the schedule can never
        // finish before its LARGEST unit, so
        //     speedup_bound = total_work / largest_unit
        // with per-function count as the (crude, honest) work proxy. It is an
        // UPPER bound — real backends have per-unit fixed cost and the levels
        // may serialise it further. If it reads ~1.0, the partition as it
        // stands buys nothing and the report has to say so.
        double parallel_bound() const {
            if (largest_unit_fns == 0) return 1.0;
            return (double)fns_total / (double)largest_unit_fns;
        }
        // The complement of fns_non_common, spelled out: how much of the
        // module the Common residue holds. Absent, a reader infers it wrongly.
        size_t fns_common() const { return fns_total - fns_non_common; }
    };
    Census census() const;

    // Writ-SDN sidecar.
    //
    // ⚠ `used_order` IS ALWAYS EMPTY TODAY, and saying otherwise was the point
    // of this correction. Both call sites pass `{}` (emit_module.cpp, main.cpp)
    // because NOTHING DRIVES THE ORDER YET: `levels()` has no consumer, so there
    // is no "order the driver actually walked" to record. The comment that used
    // to stand here described the intended end state in the present tense — a
    // justification that had drifted from its mechanism, in the very file
    // written to keep the graph honest. That is this project's most-repeated
    // defect class, so it is called out rather than quietly rewritten.
    //
    // The parameter is kept because slice 2 (ordering) is what fills it, and at
    // that point the gate CAN compare the recorded walk against
    // flatten(levels()) — a real two-directional check. Until a caller passes a
    // non-empty vector, the field means "not driven", and the gate must not
    // read it as agreement.
    void write_sidecar(const std::string& path,
                       const std::vector<uint32_t>& used_order = {}) const;

    // Canary (§6.1). Two independent derivations of one fact: the unit the
    // EMITTER declared, and the family hash tag the MANGLER independently baked
    // into every symbol of that family (in BOTH spellings — reading only `Hs`
    // and missing `hs_` undercounts by 32%, measured).
    //
    // It checks the PROPERTY, not the spelling: all declared functions carrying
    // one family tag must land in ONE unit, and one unit must not absorb two
    // families. Comparing the key TEXT against a reconstructed "ctr:Hs<hex>"
    // would fail on the `container Foo {…}` seam, whose key is the written name
    // — a difference in naming, not a defect in the partition.
    struct VerifyResult {
        size_t tags_seen = 0;      // distinct family tags found in link names
        size_t fns_tagged = 0;     // functions carrying one
        size_t split_families = 0; // one family across >1 GENERATED unit → DEFECT
        size_t merged_units = 0;   // one generated unit holding >1 family → DEFECT
        // Tagged functions DECLARED into Common. NOT a defect on its own, and
        // this is a measured limit of the canary rather than a hidden one: a
        // `deem` query over family F is named after F and carries F's tag, but
        // it is not part of F — it is a consumer. The tag cannot tell a
        // family's own method from code that merely mentions the family, so
        // this count is REPORTED, never fatal. Measured on container_item_e2e,
        // where deem-generated query code produced exactly this.
        size_t tagged_in_common = 0;
        size_t bad() const { return split_families + merged_units; }
    };
    // `tags_seen == 0` means the canary examined NOTHING. A caller that treats
    // that as a pass has a check that cannot fail; the gate asserts it is > 0.
    VerifyResult verify_against_mangled_tags(const lir::LProgram& post_mono,
                                             std::vector<std::string>& out_msgs) const;

private:
    UnitGraph() = default;   // build() is the only way in

    void compute_levels();

    std::vector<UnitId>   units_;
    std::vector<UnitEdge> edges_;
    std::unordered_map<std::string, uint32_t> by_key_;
    std::vector<uint32_t> ast_unit_;   // ast idx → unit (kNone for deps)
    std::vector<uint32_t> scc_;        // unit → scc id
    std::vector<std::vector<UnitGroup>> levels_;
    std::unordered_map<std::string, uint32_t> owner_;   // link name → unit
    std::vector<size_t> unit_fn_count_;
    Census stats_{};
    bool   order_established_ = false;
    size_t order_facts_ = 0, order_rounds_ = 0;
    size_t unresolved_providers_ = 0, external_providers_ = 0;
};

} // namespace logos::compiler
