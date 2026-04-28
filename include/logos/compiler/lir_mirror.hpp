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
    // Stage 3g.2: pointer-keyed forward maps still drive per-table dedup
    // (so reverse maps stay consistent across multiple table instances —
    // e.g. sema's prog.mirror_table vs mono's out_.mirror_table). Consumer
    // hot-path reads (`expr_ref_of(LExpr&)` etc.) bypass these maps and
    // hit `LExpr::mirror_offset_` directly — that field is the cross-table
    // back-pointer set on first emission.
    std::unordered_map<const lir::LExpr*,    hermes::arena_offset_t> expr;
    std::unordered_map<const lir::LStmt*,    hermes::arena_offset_t> stmt;
    std::unordered_map<const lir::LBlock*,   hermes::arena_offset_t> block;
    std::unordered_map<const lir::Pattern*,  hermes::arena_offset_t> pat;
    std::unordered_map<const lir::HermesVal*, hermes::arena_offset_t> hermes_val;

    // Reverse maps. Populated alongside their forward counterparts so view
    // code can descend back to the variant tree where the recursion target
    // is still a C++ struct (e.g. visit_block(LBlock&) during Phase 3d).
    std::unordered_map<uint32_t, const lir::LBlock*>     block_by_offset;
    std::unordered_map<uint32_t, const lir::LExpr*>      expr_by_offset;
    std::unordered_map<uint32_t, const lir::LStmt*>      stmt_by_offset;
    std::unordered_map<uint32_t, const lir::HermesVal*>  hermes_val_by_offset;

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

// Emit mirror entries for a single function body into an existing table.
// Used by mono to mirror each cloned/instantiated function as it is produced
// (so scan_fn / borrow_check can read the variant tree via mirror dispatch).
// Skips extern / metaprog_stub / from_binary_module functions, like run().
void lir_mirror_emit_function(lir::LProgram& prog,
                              LirMirrorTable& table,
                              lir::LFunction& fn);

// Run the full emit driver but extend an existing table rather than create a
// fresh one. Used by mono as a fixup pass to cover items not reached via the
// per-function path (consts, impls, struct methods of non-instantiated
// structs). Already-emitted nodes are deduplicated by the table caches.
void lir_mirror_emit_into(lir::LProgram& prog, LirMirrorTable& table);

// Stage 2 — direct mirror writers (no variant). Allocate a fresh mirror map
// for a single expr kind directly from primitive args, without going through
// LExpr::kind variant. Used by LirBuilder / mono_clone after Stage 2 retires
// the variant alternative for that kind. Caller assigns the returned offset
// to `LExpr::mirror_offset_`.
hermes::arena_offset_t lir_mirror_emit_lit_bool(lir::LProgram& prog, TypeRef ty, bool v);
hermes::arena_offset_t lir_mirror_emit_lit_int  (lir::LProgram& prog, TypeRef ty, int64_t v);
hermes::arena_offset_t lir_mirror_emit_lit_float(lir::LProgram& prog, TypeRef ty, double v);
hermes::arena_offset_t lir_mirror_emit_lit_str  (lir::LProgram& prog, TypeRef ty, std::string_view v);
hermes::arena_offset_t lir_mirror_emit_var_ref  (lir::LProgram& prog, TypeRef ty, std::string_view name);
hermes::arena_offset_t lir_mirror_emit_addr_of  (lir::LProgram& prog, TypeRef ty, std::string_view var_name);
hermes::arena_offset_t lir_mirror_emit_pack_expand(lir::LProgram& prog, TypeRef ty, std::string_view var_name);
hermes::arena_offset_t lir_mirror_emit_size_of      (lir::LProgram& prog, TypeRef ty, TypeRef elem);
hermes::arena_offset_t lir_mirror_emit_type_code_of (lir::LProgram& prog, TypeRef ty, TypeRef elem);
hermes::arena_offset_t lir_mirror_emit_reflect_of   (lir::LProgram& prog, TypeRef ty, TypeRef elem);

// Update the TYPE field of an existing expr mirror in-place. Used by
// LirBuilder::retype_expr so retyping doesn't need to re-walk the variant
// (post-Stage-2 the variant is the wrong source of truth for retired kinds).
void lir_mirror_retype_expr(lir::LProgram& prog,
                            hermes::arena_offset_t expr_off,
                            TypeRef new_ty);

// Cache-only walker for items moved wholesale from in_ → out_ during mono
// (impl methods, const value exprs). These nodes carry mirror_offset_ from
// sema's emit pass, but the fresh out_.mirror_table cache is empty for them.
// Walks impls/consts and back-fills the cache via the mirror_offset_ != 0
// branch of emit_*; no new mirror nodes are allocated.
void lir_mirror_populate_moved(lir::LProgram& prog, LirMirrorTable& table);

// Stage 3g.1 — per-node entry points. Used by LirBuilder to emit a mirror
// for a single freshly-constructed node (and any of its children that are
// not yet in the table). Idempotent: calling on an already-mirrored node
// is a cache hit and returns the existing offset.
//
// All four require `prog.mirror_table` to be non-null (LProgram() now
// initializes it eagerly). The arena is `prog.type_pool.arena_or_init()`.
hermes::arena_offset_t lir_mirror_emit_expr_node (lir::LProgram& prog, const lir::LExpr&     e);
hermes::arena_offset_t lir_mirror_emit_stmt_node (lir::LProgram& prog, const lir::LStmt&     s);
hermes::arena_offset_t lir_mirror_emit_block_node(lir::LProgram& prog, const lir::LBlock&    b);
hermes::arena_offset_t lir_mirror_emit_pat_node  (lir::LProgram& prog, const lir::Pattern&   p);
hermes::arena_offset_t lir_mirror_emit_hv_node   (lir::LProgram& prog, const lir::HermesVal& v);

} // namespace logos::compiler
