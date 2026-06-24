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
#include <logos/compiler/lir_schema.hpp>
#include <logos/hermes/compat.hpp>

#include <memory>
#include <string_view>
#include <unordered_map>

namespace logos::compiler {

class DeclArrayBuilder;

// Direct-Hermes decl factory (Stage E, Victor's direct-build). Builds a decl
// mirror map STRAIGHT into prog's HermesCtr — no C++ Draft heap buffer. Typed
// setters + array builders; the map stays open so multi-step construction (e.g.
// lower_trait_def then apply_annots_to_trait) writes into the same mirror in
// place (Hermes objects are mutable; collection headers are stable across grow).
// Sparse setters skip empty/zero/false. finish via addr() / view<V>().
class DeclBuilder {
public:
    // Create a new decl map of `kind` (cap = max key count, see make_map).
    DeclBuilder(lir::LProgram& prog, lir_schema::decl::Code kind, uint64_t cap);
    // Re-open an existing decl map to add/overwrite fields in place.
    DeclBuilder(lir::LProgram& prog, const uint8_t* existing_map);
    ~DeclBuilder();
    DeclBuilder(DeclBuilder&&) noexcept;
    DeclBuilder& operator=(DeclBuilder&&) noexcept;
    DeclBuilder(const DeclBuilder&) = delete;

    void str(lir_schema::Key key, std::string_view v);   // skip when empty
    void str_always(lir_schema::Key key, std::string_view v);
    void i64(lir_schema::Key key, int64_t v);            // always
    void i64_if(lir_schema::Key key, int64_t v);         // skip when 0
    void flag(lir_schema::Key key, bool v);              // sparse — only when true
    void bool_always(lir_schema::Key key, bool v);       // always — put_bool(v)
    void type(lir_schema::Key key, TypeRef t);           // skip when null
    void expr(lir_schema::Key key, lir_view::ExprRef e); // RelPtr<LExpr> — skip when null
    void block(lir_schema::Key key, lir_view::BlockRef b);// RelPtr<block> — skip when null
    // ExternalRef Pod niche — skip when the ref's arena_id is invalid.
    void ext_ref(lir_schema::Key key, hermes::ExternalRef r);
    DeclArrayBuilder array(lir_schema::Key key);         // create empty array under key

    const uint8_t* addr() const noexcept;
    const hermes::Arena* arena() const noexcept;
    template <class V> V view() const noexcept {
        return V{lir_view::DeclRef(arena(), addr())};
    }
private:
    struct Impl;
    DeclBuilder(std::unique_ptr<Impl> p) noexcept;
    std::unique_ptr<Impl> p_;
    friend class DeclArrayBuilder;
};

// Builds the elements of one array created via DeclBuilder::array().
class DeclArrayBuilder {
public:
    void push_str(std::string_view v);
    void push_type(TypeRef t);                 // skip when null
    void push_ref(const uint8_t* child_addr);  // push an existing mirror by address
    // Create a sub-map element of `schema_code` (use stmt::Count+N), push it,
    // and return a builder to populate it.
    DeclBuilder submap(uint64_t schema_code, uint64_t cap);
    // Reuse the emitter's canonical sub-map encoders (so bounds/params match
    // the schema used elsewhere).
    void push_tbound(const TraitBound& tb);
    void push_param(const lir::LParam& p);
    void push_fn_tparam(const TypeParam& tp);
    // WHERE_TYPE_BOUNDS element — delegate to the emitter's wherebound_av.
    void push_wherebound(const std::pair<TypeRef, std::string>& wb);
    // Struct decl array elements — delegate to the emitter's field_av / annot_av
    // (FIELDS / ANNOTATIONS), mirroring the recursive sub-map encoders.
    void push_field(const lir::LField& fld);
    void push_annotation(const lir::LAnnotationInstance& ai);
private:
    DeclArrayBuilder(DeclBuilder::Impl* owner, const uint8_t* arr_addr) noexcept
        : owner_(owner), arr_(arr_addr) {}
    DeclBuilder::Impl* owner_;
    const uint8_t*     arr_;
    friend class DeclBuilder;
};

// Side table populated by lir_mirror_emit. Read-only after emit completes.
// Forward maps only: C++ node pointer → its TinyObjectMap mirror ADDRESS in the
// TypePool arena. Stage D removed the reverse (mirror→node) maps — every consumer
// reads the mirror via lir_view, so no mirror→C++ lookup remains.
struct LirMirrorTable {
    // Pointer-keyed forward maps drive per-table emit dedup (idempotent
    // re-emission across multiple table instances — sema's prog.mirror_table vs
    // mono's out_.mirror_table). Consumer reads go straight through the node's
    // own mirror_ptr_ / a lir_view ref, not these maps.
    std::unordered_map<const lir::Pattern*,  const uint8_t*> pat;
    std::unordered_map<const lir::HermesVal*, const uint8_t*> hermes_val;


    // Step 7b: closure_box mirror-addr → its EClosure*. Populated by
    // LirBuilder::closure_box and read by closure_to_fnptr to retrieve
    // the inner EClosure pointer without going through the variant.
    // Keyed by the ClosureBox mirror's absolute address (ExprRef::addr()),
    // the stable node identity now that the husk LExpr* is gone.
    std::unordered_map<const uint8_t*, lir::EClosure*> closure_box_inner;

    bool empty() const noexcept {
        return pat.empty()
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

// Append a method (already-emitted FunctionView) to a stored struct's mutable
// METHODS array IN PLACE — for the sema/mono passes that collect struct methods
// AFTER the struct is stored. Sound: the ObjectArray header is stable across
// grow(). Replaces `target->methods.push_back(fn)` once structs are StructViews.
void lir_mirror_struct_append_method(lir::LProgram& prog,
                                     lir_view::StructView sv,
                                     lir_view::FunctionView m);

// Replace a stored struct's METHODS array with exactly `ms` (SemaCache reset's
// filter_methods, which keeps only binary-origin methods). Old array → garbage.
void lir_mirror_struct_set_methods(lir::LProgram& prog,
                                   lir_view::StructView sv,
                                   const std::vector<lir_view::FunctionView>& ms);

// Set/overwrite a stored struct's TYPE_CODE scalar in place. Used by sema when
// a trait-declared / explicit type_code is applied to a struct AFTER it is
// stored (sema_decl trait type_code inheritance + explicit_type_codes_ apply).
void lir_mirror_struct_set_type_code(lir::LProgram& prog,
                                     lir_view::StructView sv,
                                     uint64_t code);

// Run the full emit driver but extend an existing table rather than create a
// fresh one. Used by mono as a fixup pass to cover items not reached via the
// per-function path (consts, impls, struct methods of non-instantiated
// structs). Already-emitted nodes are deduplicated by the table caches.
void lir_mirror_emit_into(lir::LProgram& prog, LirMirrorTable& table);

// Stage 2 — direct mirror writers (no variant). Allocate a fresh mirror map
// for a single expr kind directly from primitive args, without going through
// LExpr::kind variant. Used by LirBuilder / mono_clone after Stage 2 retires
// the variant alternative for that kind. Caller assigns the returned offset
// to `LExpr::mirror_ptr_`.
const uint8_t* lir_mirror_emit_lit_bool(lir::LProgram& prog, TypeRef ty, bool v);
const uint8_t* lir_mirror_emit_lit_int  (lir::LProgram& prog, TypeRef ty, int64_t v);
const uint8_t* lir_mirror_emit_lit_int_128(lir::LProgram& prog, TypeRef ty,
                                                   uint64_t lo, uint64_t hi);
const uint8_t* lir_mirror_emit_lit_float(lir::LProgram& prog, TypeRef ty, double v);
const uint8_t* lir_mirror_emit_lit_str  (lir::LProgram& prog, TypeRef ty, std::string_view v);
const uint8_t* lir_mirror_emit_var_ref  (lir::LProgram& prog, TypeRef ty, std::string_view name,
                                                 uint32_t slot = 0xFFFFFFFFu);
const uint8_t* lir_mirror_emit_addr_of  (lir::LProgram& prog, TypeRef ty, std::string_view var_name);
const uint8_t* lir_mirror_emit_pack_expand(lir::LProgram& prog, TypeRef ty, std::string_view var_name);

// Stage E — ObjectMap (working-state map) put helpers. Create the map in prog's
// arena on first put; ref carries the stable header addr afterwards.
void lir_mirror_map_put_str (lir::LProgram& prog, lir_view::ObjectMapRef& ref,
                             std::string_view key, std::string_view val);
void lir_mirror_map_put_ref (lir::LProgram& prog, lir_view::ObjectMapRef& ref,
                             std::string_view key, const uint8_t* val_addr);
void lir_mirror_map_put_null(lir::LProgram& prog, lir_view::ObjectMapRef& ref,
                             std::string_view key);

// Stage E — declaration-layer mirror writers.
const uint8_t* lir_mirror_emit_type_alias(lir::LProgram& prog, std::string_view name,
                                          TypeRef type, std::string_view doc);
const uint8_t* lir_mirror_emit_size_of      (lir::LProgram& prog, TypeRef ty, TypeRef elem);
const uint8_t* lir_mirror_emit_align_of     (lir::LProgram& prog, TypeRef ty, TypeRef elem);
const uint8_t* lir_mirror_emit_generic_ref  (lir::LProgram& prog, TypeRef ty, std::string_view name, const std::vector<TypeRef>& type_args);
const uint8_t* lir_mirror_emit_type_code_of (lir::LProgram& prog, TypeRef ty, TypeRef elem);
const uint8_t* lir_mirror_emit_reflect_of   (lir::LProgram& prog, TypeRef ty, TypeRef elem);

// Stage 2 Group 1 — children-only expr kinds. Caller has already built each
// child (with mirror_ptr_ set). Helper recursively emit_av's children via
// the cache-hit fast path.
const uint8_t* lir_mirror_emit_enum_lit     (lir::LProgram& prog, TypeRef ty, std::string_view enum_name, std::string_view variant, int64_t disc);
const uint8_t* lir_mirror_emit_enum_lit_data(lir::LProgram& prog, TypeRef ty, std::string_view enum_name, std::string_view variant, int64_t disc, const std::vector<lir_view::ExprRef>& payload);
const uint8_t* lir_mirror_emit_struct_lit   (lir::LProgram& prog, TypeRef ty, std::string_view name, const std::vector<std::pair<std::string, lir_view::ExprRef>>& fields);
const uint8_t* lir_mirror_emit_call         (lir::LProgram& prog, TypeRef ty, std::string_view callee, const std::vector<TypeRef>& type_args, const std::vector<lir_view::ExprRef>& args);
const uint8_t* lir_mirror_emit_method_call  (lir::LProgram& prog, TypeRef ty, lir_view::ExprRef receiver, std::string_view method, std::string_view resolved_symbol, const std::vector<TypeRef>& type_args, const std::vector<lir_view::ExprRef>& args, int32_t vtable_index, std::string_view resolved_type, std::string_view tag_system, std::string_view tag_trait);
const uint8_t* lir_mirror_emit_unary        (lir::LProgram& prog, TypeRef ty, std::string_view op, lir_view::ExprRef operand);
const uint8_t* lir_mirror_emit_bin_op       (lir::LProgram& prog, TypeRef ty, std::string_view op, lir_view::ExprRef lhs, lir_view::ExprRef rhs);
const uint8_t* lir_mirror_emit_field_read   (lir::LProgram& prog, TypeRef ty, lir_view::ExprRef receiver, std::string_view field);
const uint8_t* lir_mirror_emit_index_read   (lir::LProgram& prog, TypeRef ty, lir_view::ExprRef receiver, lir_view::ExprRef index);
const uint8_t* lir_mirror_emit_hermes_lit   (lir::LProgram& prog, TypeRef ty, const lir::HermesValPtr& root, bool has_captures, const std::vector<lir_view::ExprRef>& capture_exprs, const std::vector<TypeRef>& capture_types, uint32_t capture_param_count, std::string_view static_blob = {});
const uint8_t* lir_mirror_emit_deref        (lir::LProgram& prog, TypeRef ty, lir_view::ExprRef operand);
const uint8_t* lir_mirror_emit_cast         (lir::LProgram& prog, TypeRef ty, lir_view::ExprRef operand, std::string_view hermes_build_fn);
const uint8_t* lir_mirror_emit_try          (lir::LProgram& prog, TypeRef ty, lir_view::ExprRef inner, int32_t ok_disc, int32_t err_disc);
const uint8_t* lir_mirror_emit_slice_lit    (lir::LProgram& prog, TypeRef ty, lir_view::ExprRef base, lir_view::ExprRef len);
const uint8_t* lir_mirror_emit_slice_index  (lir::LProgram& prog, TypeRef ty, lir_view::ExprRef slice, lir_view::ExprRef index);
const uint8_t* lir_mirror_emit_slice_len    (lir::LProgram& prog, TypeRef ty, lir_view::ExprRef slice);
const uint8_t* lir_mirror_emit_slice_ptr    (lir::LProgram& prog, TypeRef ty, lir_view::ExprRef slice);
const uint8_t* lir_mirror_emit_addr_of_temp (lir::LProgram& prog, TypeRef ty, lir_view::ExprRef inner, bool is_mut);
const uint8_t* lir_mirror_emit_ptr_arith    (lir::LProgram& prog, TypeRef ty, uint8_t op, lir_view::ExprRef ptr, lir_view::ExprRef offset);
const uint8_t* lir_mirror_emit_ptr_diff     (lir::LProgram& prog, TypeRef ty, bool by_byte, lir_view::ExprRef lhs, lir_view::ExprRef rhs);
const uint8_t* lir_mirror_emit_if_expr      (lir::LProgram& prog, TypeRef ty, lir_view::ExprRef cond, lir_view::ExprRef then_val, lir_view::ExprRef else_val);
const uint8_t* lir_mirror_emit_tuple_lit    (lir::LProgram& prog, TypeRef ty, const std::vector<lir_view::ExprRef>& elems);
const uint8_t* lir_mirror_emit_tuple_index  (lir::LProgram& prog, TypeRef ty, lir_view::ExprRef receiver, uint32_t index);
const uint8_t* lir_mirror_emit_arr_lit      (lir::LProgram& prog, TypeRef ty, const std::vector<lir_view::ExprRef>& elems);
const uint8_t* lir_mirror_emit_block_expr   (lir::LProgram& prog, TypeRef ty, lir_view::BlockRef block, lir_view::ExprRef result);
// EAGER block completion: emit a block container from collected stmt refs.
lir_view::BlockRef lir_mirror_block         (lir::LProgram& prog, const std::vector<lir_view::StmtRef>& stmts);
const uint8_t* lir_mirror_emit_closure_call (lir::LProgram& prog, TypeRef ty, lir_view::ExprRef callee, const std::vector<lir_view::ExprRef>& args);
const uint8_t* lir_mirror_emit_fn_ptr_call  (lir::LProgram& prog, TypeRef ty, lir_view::ExprRef callee, const std::vector<lir_view::ExprRef>& args);
const uint8_t* lir_mirror_emit_match_expr   (lir::LProgram& prog, TypeRef ty, lir_view::ExprRef scrut, const std::vector<lir::EMatchArm>& arms);
const uint8_t* lir_mirror_emit_match_expr   (lir::LProgram& prog, TypeRef ty, lir_view::ExprRef scrut, const std::vector<lir::EMatchArmView>& arms);
const uint8_t* lir_mirror_emit_format_call  (lir::LProgram& prog, TypeRef ty, lir_view::ExprRef fmt, const std::vector<lir_view::ExprRef>& args, const std::vector<TypeRef>& arg_types);
const uint8_t* lir_mirror_emit_closure_box  (lir::LProgram& prog, TypeRef ty, const lir::EClosure* inner);

// Stage B.6 — LStmt direct mirror writers. Allocate a fresh mirror map for a
// single stmt kind from primitive args, without reading LStmt::kind. Caller
// assigns the returned offset to LStmt::mirror_ptr_.
const uint8_t* lir_mirror_emit_let               (lir::LProgram& prog, uint32_t line, std::string_view name, TypeRef ty, lir_view::ExprRef value, bool is_mut, uint32_t slot = 0xFFFFFFFFu);
const uint8_t* lir_mirror_emit_assign            (lir::LProgram& prog, uint32_t line, std::string_view name, lir_view::ExprRef value, bool drop_old = false);
const uint8_t* lir_mirror_emit_return            (lir::LProgram& prog, uint32_t line, lir_view::ExprRef value);
const uint8_t* lir_mirror_emit_if_stmt           (lir::LProgram& prog, uint32_t line, lir_view::ExprRef cond, lir_view::BlockRef then_blk, lir_view::BlockRef else_blk);
const uint8_t* lir_mirror_emit_while             (lir::LProgram& prog, uint32_t line, lir_view::ExprRef cond, lir_view::BlockRef body, std::string_view label);
const uint8_t* lir_mirror_emit_for               (lir::LProgram& prog, uint32_t line, std::string_view var, lir_view::ExprRef lo, lir_view::ExprRef hi, bool inclusive, lir_view::BlockRef body, std::string_view label, uint32_t slot = 0xFFFFFFFFu);
const uint8_t* lir_mirror_emit_loop              (lir::LProgram& prog, uint32_t line, lir_view::BlockRef body, std::string_view label, std::string_view break_slot, TypeRef result_type);
const uint8_t* lir_mirror_emit_break             (lir::LProgram& prog, uint32_t line, lir_view::ExprRef value, std::string_view label);
const uint8_t* lir_mirror_emit_continue          (lir::LProgram& prog, uint32_t line, std::string_view label);
const uint8_t* lir_mirror_emit_block_stmt        (lir::LProgram& prog, uint32_t line, lir_view::BlockRef body);
const uint8_t* lir_mirror_emit_field_write       (lir::LProgram& prog, uint32_t line, std::string_view receiver, std::string_view field, lir_view::ExprRef value);
const uint8_t* lir_mirror_emit_index_write       (lir::LProgram& prog, uint32_t line, std::string_view arr, lir_view::ExprRef index, lir_view::ExprRef value);
const uint8_t* lir_mirror_emit_field_index_write (lir::LProgram& prog, uint32_t line, std::string_view receiver, std::string_view field, lir_view::ExprRef index, lir_view::ExprRef value);
const uint8_t* lir_mirror_emit_expr_stmt         (lir::LProgram& prog, uint32_t line, lir_view::ExprRef expr);
const uint8_t* lir_mirror_emit_match_stmt        (lir::LProgram& prog, uint32_t line, lir_view::ExprRef scrut, const std::vector<lir::LMatchArm>& arms);
const uint8_t* lir_mirror_emit_for_each          (lir::LProgram& prog, uint32_t line, std::string_view var, lir_view::ExprRef iter, TypeRef elem_type, int64_t arr_size, bool is_slice, lir_view::BlockRef body, uint32_t slot = 0xFFFFFFFFu);
const uint8_t* lir_mirror_emit_deref_write       (lir::LProgram& prog, uint32_t line, lir_view::ExprRef ptr, lir_view::ExprRef value, bool drop_old = false);
const uint8_t* lir_mirror_emit_drop              (lir::LProgram& prog, uint32_t line, std::string_view var_name, std::string_view drop_fn, TypeRef ty, bool drop_fields, const std::vector<std::string>& moved_fields = {});
const uint8_t* lir_mirror_emit_deref_field_write (lir::LProgram& prog, uint32_t line, std::string_view receiver, std::string_view type_name, std::string_view field, lir_view::ExprRef value);
const uint8_t* lir_mirror_emit_tuple_write       (lir::LProgram& prog, uint32_t line, std::string_view receiver, uint32_t index, lir_view::ExprRef value, TypeRef recv_type);
const uint8_t* lir_mirror_emit_let_else          (lir::LProgram& prog, uint32_t line, const lir::Pattern& pat, lir_view::ExprRef scrut, lir_view::BlockRef else_block, const std::vector<lir::LExprPtr>& guards = {});
const uint8_t* lir_mirror_emit_chain_field_write (lir::LProgram& prog, uint32_t line, std::string_view receiver, std::string_view mid_field, const std::vector<std::string>& extras, std::string_view field, lir_view::ExprRef value);

// Stage B.6 — HermesVal direct mirror writers. Allocate a fresh mirror map for
// a single HV variant from primitive args, without reading HermesVal::kind.
// Caller assigns the returned offset to HermesVal::mirror_ptr_. Children
// (HermesValPtr) must already carry their own mirror_ptr_.
const uint8_t* lir_mirror_emit_hv_null    (lir::LProgram& prog);
const uint8_t* lir_mirror_emit_hv_bool    (lir::LProgram& prog, bool value);
const uint8_t* lir_mirror_emit_hv_int     (lir::LProgram& prog, int64_t value);
const uint8_t* lir_mirror_emit_hv_float   (lir::LProgram& prog, double value);
const uint8_t* lir_mirror_emit_hv_str     (lir::LProgram& prog, std::string_view value);
const uint8_t* lir_mirror_emit_hv_map     (lir::LProgram& prog, const std::vector<lir::HVMapEntry>& entries, std::string_view key_type);
const uint8_t* lir_mirror_emit_hv_array   (lir::LProgram& prog, const std::vector<lir::HermesValPtr>& elements, std::string_view elem_type);
const uint8_t* lir_mirror_emit_hv_capture (lir::LProgram& prog, uint32_t param_index, uint32_t value_index);
const uint8_t* lir_mirror_emit_hv_type    (lir::LProgram& prog, uint32_t kind, uint64_t uid, std::string_view name);

// Stage B.6 — Pattern direct mirror writers. Allocate a fresh mirror map for a
// single Pattern variant from primitive args, without reading Pattern::kind.
// Caller assigns the returned offset to Pattern::mirror_ptr_. Sub-patterns
// must already carry their own mirror_ptr_.
const uint8_t* lir_mirror_emit_pat_variant      (lir::LProgram& prog, std::string_view enum_name, std::string_view variant, int64_t disc);
const uint8_t* lir_mirror_emit_pat_int          (lir::LProgram& prog, int64_t value);
const uint8_t* lir_mirror_emit_pat_bool         (lir::LProgram& prog, bool value);
const uint8_t* lir_mirror_emit_pat_wild         (lir::LProgram& prog, std::string_view name, uint32_t slot = 0xFFFFFFFFu);
const uint8_t* lir_mirror_emit_pat_variant_data (lir::LProgram& prog, std::string_view enum_name, std::string_view variant, int64_t disc, const std::vector<std::string>& bindings, const std::vector<TypeRef>& binding_types, const std::vector<uint32_t>& bind_slots = {});
const uint8_t* lir_mirror_emit_pat_or           (lir::LProgram& prog, const std::vector<lir::Pattern>& alts);
const uint8_t* lir_mirror_emit_pat_tuple        (lir::LProgram& prog, const std::vector<std::string>& bindings, const std::vector<TypeRef>& binding_types, const std::vector<lir::Pattern>& subs, const std::vector<uint32_t>& bind_slots = {});
const uint8_t* lir_mirror_emit_pat_range        (lir::LProgram& prog, int64_t lo, int64_t hi);
const uint8_t* lir_mirror_emit_pat_struct       (lir::LProgram& prog, std::string_view struct_name, const std::vector<lir::PatFieldBinding>& fields, bool has_rest);
const uint8_t* lir_mirror_emit_pat_slice        (lir::LProgram& prog, const std::vector<lir::Pattern>& prefix, const std::vector<lir::Pattern>& rest, const std::vector<lir::Pattern>& suffix);
const uint8_t* lir_mirror_emit_pat_at           (lir::LProgram& prog, std::string_view name, const std::vector<lir::Pattern>& sub, TypeRef type, uint32_t slot = 0xFFFFFFFFu);
const uint8_t* lir_mirror_emit_pat_ref_bind     (lir::LProgram& prog, std::string_view name, bool is_mut, TypeRef bind_type, uint32_t slot = 0xFFFFFFFFu);
const uint8_t* lir_mirror_emit_pat_ref_pat      (lir::LProgram& prog, const std::vector<lir::Pattern>& inner, bool is_mut);

// Update the TYPE field of an existing expr mirror in-place. Used by
// LirBuilder::retype_expr so retyping doesn't need to re-walk the variant
// (post-Stage-2 the variant is the wrong source of truth for retired kinds).
void lir_mirror_retype_expr(lir::LProgram& prog,
                            const uint8_t* expr_addr,
                            TypeRef new_ty);

// Cache-only walker for items moved wholesale from in_ → out_ during mono
// (impl methods, const value exprs). These nodes carry mirror_ptr_ from
// sema's emit pass, but the fresh out_.mirror_table cache is empty for them.
// Walks impls/consts and back-fills the cache via the mirror_ptr_ != 0
// branch of emit_*; no new mirror nodes are allocated.
void lir_mirror_populate_moved(lir::LProgram& prog, LirMirrorTable& table);

// Stage 3g.1 — per-node entry points. Used by LirBuilder to emit a mirror
// for a single freshly-constructed node (and any of its children that are
// not yet in the table). Idempotent: calling on an already-mirrored node
// is a cache hit and returns the existing offset.
//
// All four require `prog.mirror_table` to be non-null (LProgram() now
// initializes it eagerly). The arena is `prog.type_pool.arena_or_init()`.
const uint8_t* lir_mirror_emit_pat_node  (lir::LProgram& prog, const lir::Pattern&   p);
const uint8_t* lir_mirror_emit_hv_node   (lir::LProgram& prog, const lir::HermesVal& v);

} // namespace logos::compiler
