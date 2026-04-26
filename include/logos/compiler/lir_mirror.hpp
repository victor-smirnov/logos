#pragma once

// Phase 3b — Hermes mirror emitter for L-IR.
//
// `lir_mirror_emit(prog)` walks every function in `prog` and writes a Hermes
// TinyObjectMap mirror for each LExpr / LStmt / LBlock / Pattern / sub-node
// into the program's TypePool arena (so type and L-IR mirrors share a single
// offset space — see plans/snappy-knitting-kay.md, "single Hermes document
// per compilation").
//
// Phase 3b is write-only: the mirror exists alongside the std::variant tree
// but no consumers read it yet. Phase 3d (read-side migration) flips
// borrow_check / mono / mlir_gen to consume the mirror via view types, after
// which Phase 3e retires the std::variant.
//
// Back-references from C++ nodes to mirror offsets are kept in a side table
// on LProgram (LirMirrorTable) so the existing structs do not change layout
// during the transitional period.

#include <logos/compiler/lir.hpp>
#include <logos/hermes/arena.hpp>

#include <unordered_map>

namespace logos::compiler {

// Side table populated by lir_mirror_emit. Read-only after emit completes.
// Keyed by raw pointer into the std::variant tree because LExpr/LStmt nodes
// are uniquely owned (no aliasing); a successful lookup yields the offset of
// the corresponding TinyObjectMap inside the program's TypePool arena.
struct LirMirrorTable {
    std::unordered_map<const lir::LExpr*,    hermes::arena_offset_t> expr;
    std::unordered_map<const lir::LStmt*,    hermes::arena_offset_t> stmt;
    std::unordered_map<const lir::LBlock*,   hermes::arena_offset_t> block;
    std::unordered_map<const lir::Pattern*,  hermes::arena_offset_t> pat;
    std::unordered_map<const lir::HermesVal*, hermes::arena_offset_t> hermes_val;

    // Reverse maps. Populated alongside their forward counterparts so view
    // code can descend back to the variant tree where the recursion target
    // is still a C++ struct (e.g. visit_block(LBlock&) during Phase 3d).
    std::unordered_map<uint32_t, const lir::LBlock*> block_by_offset;
    std::unordered_map<uint32_t, const lir::LExpr*>  expr_by_offset;

    bool empty() const noexcept {
        return expr.empty() && stmt.empty() && block.empty() && pat.empty()
            && hermes_val.empty();
    }
};

// Emit Hermes mirrors for every function body in `prog`. The mirror lives in
// `prog.type_pool.arena_or_init()`. Side-table back-references are stored in
// the returned LirMirrorTable (caller may attach to LProgram).
//
// Idempotent: re-emitting on the same prog produces a fresh table (and fresh
// mirror nodes; no de-duplication — L-IR has no interning).
LirMirrorTable lir_mirror_emit(lir::LProgram& prog);

} // namespace logos::compiler
