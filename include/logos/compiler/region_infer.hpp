#pragma once
// B70: region inference scaffolding — per-borrow region variables, CFG
// over LFunction body, borrow-site collection. Standalone analysis: at
// this slice it builds the data structures and runs an empty solver
// hook (B71 will populate the solver, B72 will replace min-viable NLL
// with the conflict checker driven by these regions).
//
// Design notes:
//   - Each `&x` / `&mut x` (LIR AddrOf, AddrOfTemp) in the fn body is
//     assigned a fresh `RegionId`. The region's "live set" is the set
//     of CFG points where the borrow is required to be valid — the
//     statement that creates it plus everywhere a value flowing from
//     it is used.
//   - The CFG is statement-granular. Each statement gets a unique
//     `StmtPoint`. Successor edges encode sequential flow, if/else
//     join, loop back-edges, returns/breaks/continues.
//   - Existing min-viable NLL (B61) stays active until B72 replaces it.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <logos/compiler/lir_view.hpp>  // lir_view::BlockRef/StmtRef (Stage D)

namespace logos::compiler::lir {
    struct LFunction; struct LBlock; struct LStmt;
    struct LProgram;
}

namespace logos::compiler {

// Stable id for a region. Zero is the no-region sentinel.
struct RegionId {
    uint32_t value = 0;
    bool operator==(RegionId o) const noexcept { return value == o.value; }
    bool operator!=(RegionId o) const noexcept { return value != o.value; }
    bool valid() const noexcept { return value != 0; }
};

struct RegionIdHash {
    std::size_t operator()(RegionId r) const noexcept {
        return std::hash<uint32_t>{}(r.value);
    }
};

constexpr RegionId NO_REGION{0};

// A point in the function's CFG. `block` is the depth-first block id
// (per RegionInferer's internal numbering), `idx` is the statement
// index within that block.
struct StmtPoint {
    uint32_t block = 0;
    uint32_t idx   = 0;
    bool operator==(StmtPoint o) const noexcept {
        return block == o.block && idx == o.idx;
    }
};

struct StmtPointHash {
    std::size_t operator()(StmtPoint p) const noexcept {
        return (std::size_t(p.block) << 32) ^ p.idx;
    }
};

// A borrow site — one `&x` / `&mut x` occurrence in the fn body.
struct BorrowSite {
    RegionId    region;    // freshly assigned
    StmtPoint   origin;    // where the borrow was created
    std::string holder;    // let/assign LHS that binds the borrow; empty = transient (e.g. call-arg)
    std::string target;    // the variable being borrowed FROM
    bool        is_mut = false;
    // B82: this borrow was taken as a `&mut` argument in a fn-call
    // (two-phase reservation). Conflict-finder treats it as compatible
    // with concurrent shared reads of the same target.
    bool        is_tpb_reservation = false;
    uint32_t    origin_line = 0;  // B73: source line for diagnostics
};

// A region constraint. The solver propagates region-membership across
// constraints to find a fixed point.
struct RegionConstraint {
    enum class Kind : uint8_t {
        Outlives,   // longer ⊇ shorter (longer outlives shorter)
        Contains,   // region must contain the point
    };
    Kind kind;
    RegionId longer;
    RegionId shorter;
    StmtPoint point;
};

// Per-function CFG. Block ids are dense [0..n_blocks). Each block has
// a list of statement indices [0..stmts.size()) and successor block
// ids (multiple for branches, single for sequential, empty for
// terminators).
struct CFG {
    struct Block {
        // For each statement in this block: a StmtPoint (block, idx).
        // Stored implicitly — `block=this_id, idx=0..n-1`.
        uint32_t n_stmts = 0;
        std::vector<uint32_t> successors;  // block ids
    };
    std::vector<Block> blocks;
};

// Front-end of the inference. Walks an LFunction body once: assigns
// RegionIds to every borrow expression, builds the CFG, collects
// borrow sites and seed constraints (containment at origin, declared
// outlives bounds from fn.lifetime_outlives mapped onto named regions).
class RegionInferer {
public:
    RegionInferer() = default;

    // Run the analysis pass over `fn`. Populates `cfg_`, `borrows_`,
    // and `constraints_`. `prog` supplies the type-pool arena needed
    // to materialize lir_view::{ExprRef, StmtRef} from each LStmt's
    // and LExpr's `mirror_ptr_`.
    void analyze(const lir::LFunction& fn, const lir::LProgram& prog);

    // Dump everything to stderr in a stable, grep-friendly format.
    // Triggered by env var LOGOS_DUMP_REGIONS.
    void dump(const std::string& fn_name) const;

    // Accessors for the solver (B71) and conflict checker (B72).
    const CFG& cfg() const noexcept { return cfg_; }
    const std::vector<BorrowSite>& borrows() const noexcept { return borrows_; }
    const std::vector<RegionConstraint>& constraints() const noexcept {
        return constraints_;
    }
    uint32_t region_count() const noexcept { return next_region_id_ - 1; }

    // B71.1: per-statement liveness. live_in(P) = vars whose value
    // must be available on entry to P; live_out(P) = ditto on exit.
    using LiveSet = std::unordered_set<std::string>;
    const std::unordered_map<StmtPoint, LiveSet, StmtPointHash>&
        live_in() const noexcept { return live_in_; }
    const std::unordered_map<StmtPoint, LiveSet, StmtPointHash>&
        live_out() const noexcept { return live_out_; }

    // B71.3: solved region → set of CFG points it spans. Populated by
    // analyze() after constraint generation. A region is in scope at
    // every point in its point set; B72's conflict checker reads
    // this to decide whether two borrows overlap.
    using PointSet = std::unordered_set<StmtPoint, StmtPointHash>;
    const std::unordered_map<uint32_t, PointSet>&
        region_points() const noexcept { return region_points_; }

    // B72: borrow conflict detected by overlapping regions on the
    // same target where at least one borrow is mutable. Returns the
    // list of conflicts in stable order; caller turns them into Diag
    // entries.
    struct Conflict {
        const BorrowSite* a;
        const BorrowSite* b;
        StmtPoint overlap_at;
    };
    std::vector<Conflict> find_conflicts() const;

    // logos-core 2.1: declared lifetime parameters of the current fn
    // (e.g. `'a`, `'b`) each get a fresh RegionId during analyze().
    // The map is the canonical name→region link other passes
    // (HRTB instantiation, dropck) consult.
    const std::unordered_map<std::string, RegionId>&
        named_regions() const noexcept { return named_regions_; }
    RegionId named_region(const std::string& name) const noexcept {
        auto it = named_regions_.find(name);
        return it == named_regions_.end() ? NO_REGION : it->second;
    }

    // logos-core 2.1 (consumer): does the named lifetime `longer` outlive
    // `shorter`? Reads from the SOLVED Outlives constraint graph that
    // `analyze()` populates: longer outlives shorter iff longer's point
    // set is a superset of shorter's. Equivalent to (and now the
    // canonical replacement for) borrow_check's `outlives()` BFS over
    // `outlives_adj_`. Returns true also when longer == shorter (the
    // reflexive case), and when either name is absent (lifetime not
    // declared on this fn — caller's domain to gate).
    //
    // `'static` is special-cased: it outlives every concrete `'a` and is
    // outlived by none (matches Rust's `'static: 'a` and `!('a: 'static)`).
    bool outlives_named(const std::string& longer,
                        const std::string& shorter) const noexcept;

private:
    RegionId fresh_region() noexcept {
        return RegionId{next_region_id_++};
    }
    void walk_block(lir_view::BlockRef br0, uint32_t blk_id,
                    const lir::LProgram& prog);
    void walk_stmt(lir_view::StmtRef sr, uint32_t blk_id, uint32_t idx,
                   const lir::LProgram& prog);

    // B71.1: per-statement use/def + live-in/live-out, computed by
    // a backward dataflow pass over the CFG.
    void compute_liveness();
    // B71.3: solve the constraint set into per-region point sets.
    // Contains constraints add a single point; Outlives constraints
    // propagate the shorter region's point set into the longer's.
    // Fixed-point iteration.
    void solve();
    void use_def_for_stmt(lir_view::StmtRef sr,
                          uint32_t blk_id, uint32_t idx,
                          const lir::LProgram& prog,
                          LiveSet& use, LiveSet& def) const;

    uint32_t next_region_id_ = 1;  // 0 reserved for NO_REGION
    CFG cfg_;
    std::vector<BorrowSite> borrows_;
    std::vector<RegionConstraint> constraints_;
    std::unordered_map<StmtPoint, LiveSet, StmtPointHash> live_in_;
    std::unordered_map<StmtPoint, LiveSet, StmtPointHash> live_out_;
    std::unordered_map<StmtPoint, LiveSet, StmtPointHash> use_;
    std::unordered_map<StmtPoint, LiveSet, StmtPointHash> def_;
    std::unordered_map<uint32_t, PointSet> region_points_;
    // logos-core 2.1: declared lifetime parameters of the current fn,
    // mapped to fresh RegionIds. Populated at the top of analyze() so
    // every borrow-site walk + outlives constraint generation can name
    // them by string. Outlives clauses (`'a: 'b`) seed Outlives
    // constraints between the corresponding RegionIds.
    std::unordered_map<std::string, RegionId> named_regions_;
    // The fn's raw `lifetime_outlives` pairs (longer, shorter), stored at
    // analyze() time so `outlives_named()` can BFS over the string graph
    // — equivalent to borrow_check's `outlives_adj_`, and able to walk
    // through `'static` (which is never a named-region entry but IS a
    // legal source/target in a declared `'a: 'static` clause).
    std::vector<std::pair<std::string, std::string>> outlives_pairs_;
    // B73: source line per CFG point (StmtPoint → line number from
    // the originating LStmt). Used to produce human-readable
    // diagnostics that point back at the source.
    std::unordered_map<StmtPoint, uint32_t, StmtPointHash> point_line_;
    // We keep a pointer to the program for liveness's use/def gather
    // helper which needs the type-pool arena to materialize views.
    const lir::LProgram* prog_for_liveness_ = nullptr;
};

} // namespace logos::compiler
