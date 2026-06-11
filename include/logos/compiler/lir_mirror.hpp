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
#include <logos/hermes/compat.hpp>

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

    // Step 7b: closure_box LExpr* → its EClosure*. Populated by
    // LirBuilder::closure_box and read by closure_to_fnptr to retrieve
    // the inner EClosure pointer without going through the variant.
    std::unordered_map<const lir::LExpr*, lir::EClosure*> closure_box_inner;

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
hermes::arena_offset_t lir_mirror_emit_align_of     (lir::LProgram& prog, TypeRef ty, TypeRef elem);
hermes::arena_offset_t lir_mirror_emit_generic_ref  (lir::LProgram& prog, TypeRef ty, std::string_view name, const std::vector<TypeRef>& type_args);
hermes::arena_offset_t lir_mirror_emit_type_code_of (lir::LProgram& prog, TypeRef ty, TypeRef elem);
hermes::arena_offset_t lir_mirror_emit_reflect_of   (lir::LProgram& prog, TypeRef ty, TypeRef elem);

// Stage 2 Group 1 — children-only expr kinds. Caller has already built each
// child (with mirror_offset_ set). Helper recursively emit_av's children via
// the cache-hit fast path.
hermes::arena_offset_t lir_mirror_emit_enum_lit     (lir::LProgram& prog, TypeRef ty, std::string_view enum_name, std::string_view variant, int64_t disc);
hermes::arena_offset_t lir_mirror_emit_enum_lit_data(lir::LProgram& prog, TypeRef ty, std::string_view enum_name, std::string_view variant, int64_t disc, const std::vector<lir::LExprPtr>& payload);
hermes::arena_offset_t lir_mirror_emit_struct_lit   (lir::LProgram& prog, TypeRef ty, std::string_view name, const std::vector<std::pair<std::string, lir::LExprPtr>>& fields);
hermes::arena_offset_t lir_mirror_emit_call         (lir::LProgram& prog, TypeRef ty, std::string_view callee, const std::vector<TypeRef>& type_args, const std::vector<lir::LExprPtr>& args);
hermes::arena_offset_t lir_mirror_emit_method_call  (lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& receiver, std::string_view method, std::string_view resolved_symbol, const std::vector<TypeRef>& type_args, const std::vector<lir::LExprPtr>& args, int32_t vtable_index, std::string_view resolved_type, std::string_view tag_system, std::string_view tag_trait);
hermes::arena_offset_t lir_mirror_emit_unary        (lir::LProgram& prog, TypeRef ty, std::string_view op, const lir::LExprPtr& operand);
hermes::arena_offset_t lir_mirror_emit_bin_op       (lir::LProgram& prog, TypeRef ty, std::string_view op, const lir::LExprPtr& lhs, const lir::LExprPtr& rhs);
hermes::arena_offset_t lir_mirror_emit_field_read   (lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& receiver, std::string_view field);
hermes::arena_offset_t lir_mirror_emit_index_read   (lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& receiver, const lir::LExprPtr& index);
hermes::arena_offset_t lir_mirror_emit_hermes_lit   (lir::LProgram& prog, TypeRef ty, const lir::HermesValPtr& root, bool has_captures, const std::vector<lir::LExprPtr>& capture_exprs, const std::vector<TypeRef>& capture_types, uint32_t capture_param_count, std::string_view static_blob = {});
hermes::arena_offset_t lir_mirror_emit_deref        (lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& operand);
hermes::arena_offset_t lir_mirror_emit_cast         (lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& operand, std::string_view hermes_build_fn);
hermes::arena_offset_t lir_mirror_emit_try          (lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& inner, int32_t ok_disc, int32_t err_disc);
hermes::arena_offset_t lir_mirror_emit_slice_lit    (lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& base, const lir::LExprPtr& len);
hermes::arena_offset_t lir_mirror_emit_slice_index  (lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& slice, const lir::LExprPtr& index);
hermes::arena_offset_t lir_mirror_emit_slice_len    (lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& slice);
hermes::arena_offset_t lir_mirror_emit_slice_ptr    (lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& slice);
hermes::arena_offset_t lir_mirror_emit_addr_of_temp (lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& inner, bool is_mut);
hermes::arena_offset_t lir_mirror_emit_ptr_arith    (lir::LProgram& prog, TypeRef ty, uint8_t op, const lir::LExprPtr& ptr, const lir::LExprPtr& offset);
hermes::arena_offset_t lir_mirror_emit_ptr_diff     (lir::LProgram& prog, TypeRef ty, bool by_byte, const lir::LExprPtr& lhs, const lir::LExprPtr& rhs);
hermes::arena_offset_t lir_mirror_emit_if_expr      (lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& cond, const lir::LExprPtr& then_val, const lir::LExprPtr& else_val);
hermes::arena_offset_t lir_mirror_emit_tuple_lit    (lir::LProgram& prog, TypeRef ty, const std::vector<lir::LExprPtr>& elems);
hermes::arena_offset_t lir_mirror_emit_tuple_index  (lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& receiver, uint32_t index);
hermes::arena_offset_t lir_mirror_emit_arr_lit      (lir::LProgram& prog, TypeRef ty, const std::vector<lir::LExprPtr>& elems);
hermes::arena_offset_t lir_mirror_emit_block_expr   (lir::LProgram& prog, TypeRef ty, const lir::LBlock* block, const lir::LExprPtr& result);
hermes::arena_offset_t lir_mirror_emit_closure_call (lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& callee, const std::vector<lir::LExprPtr>& args);
hermes::arena_offset_t lir_mirror_emit_fn_ptr_call  (lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& callee, const std::vector<lir::LExprPtr>& args);
hermes::arena_offset_t lir_mirror_emit_match_expr   (lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& scrut, const std::vector<lir::EMatchArm>& arms);
hermes::arena_offset_t lir_mirror_emit_format_call  (lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& fmt, const std::vector<lir::LExprPtr>& args, const std::vector<TypeRef>& arg_types);
hermes::arena_offset_t lir_mirror_emit_closure_box  (lir::LProgram& prog, TypeRef ty, const lir::EClosure* inner);

// Stage B.6 — LStmt direct mirror writers. Allocate a fresh mirror map for a
// single stmt kind from primitive args, without reading LStmt::kind. Caller
// assigns the returned offset to LStmt::mirror_offset_.
hermes::arena_offset_t lir_mirror_emit_let               (lir::LProgram& prog, uint32_t line, std::string_view name, TypeRef ty, const lir::LExprPtr& value, bool is_mut);
hermes::arena_offset_t lir_mirror_emit_assign            (lir::LProgram& prog, uint32_t line, std::string_view name, const lir::LExprPtr& value, bool drop_old = false);
hermes::arena_offset_t lir_mirror_emit_return            (lir::LProgram& prog, uint32_t line, const lir::LExprPtr& value);
hermes::arena_offset_t lir_mirror_emit_if_stmt           (lir::LProgram& prog, uint32_t line, const lir::LExprPtr& cond, const lir::LBlock* then_blk, const lir::LBlock* else_blk);
hermes::arena_offset_t lir_mirror_emit_while             (lir::LProgram& prog, uint32_t line, const lir::LExprPtr& cond, const lir::LBlock* body, std::string_view label);
hermes::arena_offset_t lir_mirror_emit_for               (lir::LProgram& prog, uint32_t line, std::string_view var, const lir::LExprPtr& lo, const lir::LExprPtr& hi, bool inclusive, const lir::LBlock* body, std::string_view label);
hermes::arena_offset_t lir_mirror_emit_loop              (lir::LProgram& prog, uint32_t line, const lir::LBlock* body, std::string_view label, std::string_view break_slot, TypeRef result_type);
hermes::arena_offset_t lir_mirror_emit_break             (lir::LProgram& prog, uint32_t line, const lir::LExprPtr& value, std::string_view label);
hermes::arena_offset_t lir_mirror_emit_continue          (lir::LProgram& prog, uint32_t line, std::string_view label);
hermes::arena_offset_t lir_mirror_emit_block_stmt        (lir::LProgram& prog, uint32_t line, const lir::LBlock* body);
hermes::arena_offset_t lir_mirror_emit_field_write       (lir::LProgram& prog, uint32_t line, std::string_view receiver, std::string_view field, const lir::LExprPtr& value);
hermes::arena_offset_t lir_mirror_emit_index_write       (lir::LProgram& prog, uint32_t line, std::string_view arr, const lir::LExprPtr& index, const lir::LExprPtr& value);
hermes::arena_offset_t lir_mirror_emit_field_index_write (lir::LProgram& prog, uint32_t line, std::string_view receiver, std::string_view field, const lir::LExprPtr& index, const lir::LExprPtr& value);
hermes::arena_offset_t lir_mirror_emit_expr_stmt         (lir::LProgram& prog, uint32_t line, const lir::LExprPtr& expr);
hermes::arena_offset_t lir_mirror_emit_match_stmt        (lir::LProgram& prog, uint32_t line, const lir::LExprPtr& scrut, const std::vector<lir::LMatchArm>& arms);
hermes::arena_offset_t lir_mirror_emit_for_each          (lir::LProgram& prog, uint32_t line, std::string_view var, const lir::LExprPtr& iter, TypeRef elem_type, int64_t arr_size, bool is_slice, const lir::LBlock* body);
hermes::arena_offset_t lir_mirror_emit_deref_write       (lir::LProgram& prog, uint32_t line, const lir::LExprPtr& ptr, const lir::LExprPtr& value);
hermes::arena_offset_t lir_mirror_emit_drop              (lir::LProgram& prog, uint32_t line, std::string_view var_name, std::string_view drop_fn, TypeRef ty, bool drop_fields, const std::vector<std::string>& moved_fields = {});
hermes::arena_offset_t lir_mirror_emit_deref_field_write (lir::LProgram& prog, uint32_t line, std::string_view receiver, std::string_view type_name, std::string_view field, const lir::LExprPtr& value);
hermes::arena_offset_t lir_mirror_emit_tuple_write       (lir::LProgram& prog, uint32_t line, std::string_view receiver, uint32_t index, const lir::LExprPtr& value, TypeRef recv_type);
hermes::arena_offset_t lir_mirror_emit_let_else          (lir::LProgram& prog, uint32_t line, const lir::Pattern& pat, const lir::LExprPtr& scrut, const lir::LBlock* else_block, const std::vector<lir::LExprPtr>& guards = {});
hermes::arena_offset_t lir_mirror_emit_chain_field_write (lir::LProgram& prog, uint32_t line, std::string_view receiver, std::string_view mid_field, const std::vector<std::string>& extras, std::string_view field, const lir::LExprPtr& value);

// Stage B.6 — HermesVal direct mirror writers. Allocate a fresh mirror map for
// a single HV variant from primitive args, without reading HermesVal::kind.
// Caller assigns the returned offset to HermesVal::mirror_offset_. Children
// (HermesValPtr) must already carry their own mirror_offset_.
hermes::arena_offset_t lir_mirror_emit_hv_null    (lir::LProgram& prog);
hermes::arena_offset_t lir_mirror_emit_hv_bool    (lir::LProgram& prog, bool value);
hermes::arena_offset_t lir_mirror_emit_hv_int     (lir::LProgram& prog, int64_t value);
hermes::arena_offset_t lir_mirror_emit_hv_float   (lir::LProgram& prog, double value);
hermes::arena_offset_t lir_mirror_emit_hv_str     (lir::LProgram& prog, std::string_view value);
hermes::arena_offset_t lir_mirror_emit_hv_map     (lir::LProgram& prog, const std::vector<lir::HVMapEntry>& entries, std::string_view key_type);
hermes::arena_offset_t lir_mirror_emit_hv_array   (lir::LProgram& prog, const std::vector<lir::HermesValPtr>& elements, std::string_view elem_type);
hermes::arena_offset_t lir_mirror_emit_hv_capture (lir::LProgram& prog, uint32_t param_index, uint32_t value_index);
hermes::arena_offset_t lir_mirror_emit_hv_type    (lir::LProgram& prog, uint32_t kind, uint64_t uid, std::string_view name);

// Stage B.6 — Pattern direct mirror writers. Allocate a fresh mirror map for a
// single Pattern variant from primitive args, without reading Pattern::kind.
// Caller assigns the returned offset to Pattern::mirror_offset_. Sub-patterns
// must already carry their own mirror_offset_.
hermes::arena_offset_t lir_mirror_emit_pat_variant      (lir::LProgram& prog, std::string_view enum_name, std::string_view variant, int64_t disc);
hermes::arena_offset_t lir_mirror_emit_pat_int          (lir::LProgram& prog, int64_t value);
hermes::arena_offset_t lir_mirror_emit_pat_bool         (lir::LProgram& prog, bool value);
hermes::arena_offset_t lir_mirror_emit_pat_wild         (lir::LProgram& prog, std::string_view name);
hermes::arena_offset_t lir_mirror_emit_pat_variant_data (lir::LProgram& prog, std::string_view enum_name, std::string_view variant, int64_t disc, const std::vector<std::string>& bindings, const std::vector<TypeRef>& binding_types);
hermes::arena_offset_t lir_mirror_emit_pat_or           (lir::LProgram& prog, const std::vector<lir::Pattern>& alts);
hermes::arena_offset_t lir_mirror_emit_pat_tuple        (lir::LProgram& prog, const std::vector<std::string>& bindings, const std::vector<TypeRef>& binding_types, const std::vector<lir::Pattern>& subs);
hermes::arena_offset_t lir_mirror_emit_pat_range        (lir::LProgram& prog, int64_t lo, int64_t hi);
hermes::arena_offset_t lir_mirror_emit_pat_struct       (lir::LProgram& prog, std::string_view struct_name, const std::vector<lir::PatFieldBinding>& fields, bool has_rest);
hermes::arena_offset_t lir_mirror_emit_pat_slice        (lir::LProgram& prog, const std::vector<lir::Pattern>& prefix, const std::vector<lir::Pattern>& rest, const std::vector<lir::Pattern>& suffix);
hermes::arena_offset_t lir_mirror_emit_pat_at           (lir::LProgram& prog, std::string_view name, const std::vector<lir::Pattern>& sub, TypeRef type);
hermes::arena_offset_t lir_mirror_emit_pat_ref_bind     (lir::LProgram& prog, std::string_view name, bool is_mut, TypeRef bind_type);
hermes::arena_offset_t lir_mirror_emit_pat_ref_pat      (lir::LProgram& prog, const std::vector<lir::Pattern>& inner, bool is_mut);

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

// Phase 5.B step 2 prerequisite: when sema modifies LExpr.type AFTER the
// LExpr has been mirrored (e.g. sema_expr.cpp:629 `inner->type = ret_t;`),
// the mirror's TYPE field becomes stale. Call this helper to overwrite the
// TYPE field in-place. Required for view-based readers (mono subst_expr
// cross-arena path, mlir_gen, borrow_check, mono_scan, region_infer) to
// see the post-construction type rather than the construction-time type.
//
// No-op when e.mirror_offset_ is 0 (the LExpr was never mirrored — sema
// path that builds without mirroring).
void lir_mirror_update_type(lir::LProgram& prog, const lir::LExpr& e);

} // namespace logos::compiler
