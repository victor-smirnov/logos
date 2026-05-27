// Logos project — https://github.com/victor-smirnov/logos
//
// Phase 3b — Hermes mirror emitter for L-IR.
//
// Walks every function/const initializer in an LProgram and writes a
// TinyObjectMap mirror per node into the program's TypePool arena. Mirrors
// are not consumed yet (Phase 3d does that); for now the emitter validates
// that every L-IR variant has a faithful Hermes representation.

#include <logos/compiler/lir_mirror.hpp>
#include <logos/compiler/lir_schema.hpp>
#include <logos/compiler/lir_view.hpp>  // header-compile smoke until 3d uses it
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/arena_value.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/schema_codes.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/verification/assert.hpp>

#include <variant>

namespace logos::compiler {

using lir::LExpr;
using lir::LStmt;
using lir::LBlock;
using lir::LMatchArm;
using lir::Pattern;
using lir::HermesVal;
using lir::EClosure;
using lir::LFunction;
using lir::LConst;
using lir::LStructDef;
using lir::LImplBlock;
using lir::LTraitDef;

namespace {

namespace ek = lir_schema::expr_keys;
namespace ec = lir_schema::expr_common;
namespace sk = lir_schema::stmt_keys;
namespace sc = lir_schema::stmt_common;
namespace pk = lir_schema::pat_keys;
namespace ak = lir_schema::arm_keys;
namespace hl = lir_schema::hermes_lit_keys;
namespace hk = lir_schema::hv_keys;
namespace ck = lir_schema::closure_keys;
namespace pdk = lir_schema::ptrdiff_keys;

class LirMirrorEmitter {
    hermes::Arena&  arena_;
    LirMirrorTable& table_;
    // Phase 5.B step 3: optional pool ref used by type_av to intern foreign
    // TypeRefs before writing their offsets into mirrors. Set by the public
    // wrappers (which have prog.type_pool in hand). Older internal callers
    // (variant walk on local prog) leave it null — in those paths every
    // TypeRef is already local so the check is a no-op anyway.
    TypePool*       pool_ = nullptr;
    // Dry-run mode: when true, make_map / make_array / put / array_push are
    // no-ops. Used to back-fill the table's reverse-lookup maps for nodes that
    // were already mirrored under a different table (e.g. carried over via
    // std::move(in_.consts)). The variant walk still recurses through child
    // emit_* calls so descendants back-fill themselves.
    bool dry_run_ = false;

public:
    LirMirrorEmitter(hermes::Arena& a, LirMirrorTable& t) : arena_(a), table_(t) {}
    LirMirrorEmitter(hermes::Arena& a, LirMirrorTable& t, TypePool& p)
        : arena_(a), table_(t), pool_(&p) {}

    void run(lir::LProgram& prog);

    void emit_function(LFunction& f) {
        if (f.is_extern || f.is_metaprog_stub || f.from_binary_module) return;
        emit_block(f.body);
    }

    // Public per-node entry points (Stage 3g.1). Called from
    // lir_mirror_emit_*_node free functions; idempotent via table cache.
    hermes::arena_offset_t emit_expr_public (const LExpr& e)    { return emit_expr(e); }
    hermes::arena_offset_t emit_stmt_public (const LStmt& s)    { return emit_stmt(s); }
    hermes::arena_offset_t emit_block_public(const LBlock& b)   { return emit_block(b); }
    hermes::arena_offset_t emit_pat_public  (const Pattern& p)  { return emit_pat(p); }
    hermes::arena_offset_t emit_hv_public   (const HermesVal& v){ return emit_hv(v); }

    // Stage 2 — variant-free direct mirror writers. Allocate a fresh map for
    // a single expr kind without reading from a variant payload. Used by
    // LirBuilder / mono_clone after Stage 2 retires the variant alternative
    // for that kind.
    hermes::arena_offset_t emit_lit_bool_direct(TypeRef ty, bool v) {
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::LitBool));
        put(map_off, ek::LIT_BOOL, put_bool(v));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_lit_int_direct(TypeRef ty, int64_t v) {
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::LitInt));
        put(map_off, ek::LIT_I64, put_i64(v));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_lit_float_direct(TypeRef ty, double v) {
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::LitFloat));
        put(map_off, ek::LIT_F64, put_f64(v));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_lit_str_direct(TypeRef ty, std::string_view v) {
        auto s_av = put_string(v);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::LitStr));
        put(map_off, ek::LIT_STR, s_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_var_ref_direct(TypeRef ty, std::string_view name) {
        auto n_av = put_string(name);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::VarRef));
        put(map_off, ek::NAME, n_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_addr_of_direct(TypeRef ty, std::string_view var_name) {
        auto n_av = put_string(var_name);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::AddrOf));
        put(map_off, ek::NAME, n_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_pack_expand_direct(TypeRef ty, std::string_view var_name) {
        auto n_av = put_string(var_name);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::PackExpand));
        put(map_off, ek::NAME, n_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_size_of_direct(TypeRef ty, TypeRef elem) {
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::SizeOf));
        put(map_off, ek::ELEM_TYPE, type_av(elem));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_align_of_direct(TypeRef ty, TypeRef elem) {
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::AlignOf));
        put(map_off, ek::ELEM_TYPE, type_av(elem));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_generic_ref_direct(TypeRef ty,
                                                    std::string_view name,
                                                    const std::vector<TypeRef>& type_args) {
        auto cn_av = put_string(name);
        auto ta_av = type_array(type_args);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::GenericRef));
        put(map_off, ek::CALLEE,    cn_av);
        put(map_off, ek::TYPE_ARGS, ta_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_type_code_of_direct(TypeRef ty, TypeRef elem) {
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::TypeCodeOf));
        put(map_off, ek::ELEM_TYPE, type_av(elem));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_reflect_of_direct(TypeRef ty, TypeRef elem) {
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::ReflectOf));
        put(map_off, ek::ELEM_TYPE, type_av(elem));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }

    // Stage 2 Group 1 — children-only direct writers. Children must already
    // have their own mirror_offset_ set (cache-hit fast path inside expr_av).
    hermes::arena_offset_t emit_deref_direct(TypeRef ty, const lir::LExprPtr& operand) {
        auto o_av = expr_av(operand);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::Deref));
        put(map_off, ek::OPERAND, o_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_cast_direct(TypeRef ty, const lir::LExprPtr& operand,
                                             std::string_view hermes_build_fn) {
        auto o_av = expr_av(operand);
        hermes::AnyVal hbf_av;
        bool has_hbf = !hermes_build_fn.empty();
        if (has_hbf) hbf_av = put_string(hermes_build_fn);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::Cast));
        put(map_off, ek::OPERAND, o_av);
        if (has_hbf) put(map_off, ek::HERMES_BUILD_FN, hbf_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_try_direct(TypeRef ty, const lir::LExprPtr& inner,
                                            int32_t ok_disc, int32_t err_disc) {
        auto in_av = expr_av(inner);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::Try));
        put(map_off, ek::INNER,    in_av);
        put(map_off, ek::OK_DISC,  put_i32(ok_disc));
        put(map_off, ek::ERR_DISC, put_i32(err_disc));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_slice_lit_direct(TypeRef ty, const lir::LExprPtr& base,
                                                  const lir::LExprPtr& len) {
        auto b_av = expr_av(base);
        auto l_av = expr_av(len);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::SliceLit));
        put(map_off, ek::BASE_PTR, b_av);
        put(map_off, ek::LEN,      l_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_slice_index_direct(TypeRef ty, const lir::LExprPtr& slice,
                                                    const lir::LExprPtr& index) {
        auto s_av = expr_av(slice);
        auto i_av = expr_av(index);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::SliceIndex));
        put(map_off, ek::SLICE, s_av);
        put(map_off, ek::INDEX, i_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_slice_len_direct(TypeRef ty, const lir::LExprPtr& slice) {
        auto s_av = expr_av(slice);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::SliceLen));
        put(map_off, ek::SLICE, s_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_slice_ptr_direct(TypeRef ty, const lir::LExprPtr& slice) {
        auto s_av = expr_av(slice);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::SlicePtr));
        put(map_off, ek::SLICE, s_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_addr_of_temp_direct(TypeRef ty, const lir::LExprPtr& inner,
                                                     bool is_mut) {
        auto in_av = expr_av(inner);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::AddrOfTemp));
        put(map_off, ek::INNER,  in_av);
        put(map_off, ek::IS_MUT, put_bool(is_mut));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_hermes_lit_direct(TypeRef ty,
                                                   const lir::HermesValPtr& root,
                                                   bool has_captures,
                                                   const std::vector<lir::LExprPtr>& capture_exprs,
                                                   const std::vector<TypeRef>& capture_types,
                                                   uint32_t capture_param_count,
                                                   std::string_view static_blob) {
        auto root_av = hv_av(root);
        auto cap_ex  = expr_array(capture_exprs);
        auto cap_ty  = type_array(capture_types);
        auto blob_av = static_blob.empty()
                       ? hermes::AnyVal{}
                       : put_string(static_blob);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::HermesLit));
        put(map_off, hl::ROOT,                  root_av);
        put(map_off, hl::HAS_CAPTURES,          put_bool(has_captures));
        put(map_off, hl::CAPTURE_EXPRS,         cap_ex);
        put(map_off, hl::CAPTURE_TYPES,         cap_ty);
        put(map_off, hl::CAPTURE_PARAM_COUNT,   put_u32(capture_param_count));
        if (!static_blob.empty()) put(map_off, hl::STATIC_BLOB, blob_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_enum_lit_direct(TypeRef ty,
                                                 std::string_view enum_name,
                                                 std::string_view variant,
                                                 int64_t disc) {
        auto en_av = put_string(enum_name);
        auto vr_av = put_string(variant);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::EnumLit));
        put(map_off, ek::ENUM_NAME, en_av);
        put(map_off, ek::VARIANT,   vr_av);
        put(map_off, ek::DISC,      put_i64(disc));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_enum_lit_data_direct(TypeRef ty,
                                                      std::string_view enum_name,
                                                      std::string_view variant,
                                                      int64_t disc,
                                                      const std::vector<lir::LExprPtr>& payload) {
        auto en_av = put_string(enum_name);
        auto vr_av = put_string(variant);
        auto pl_av = expr_array(payload);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::EnumLitData));
        put(map_off, ek::ENUM_NAME, en_av);
        put(map_off, ek::VARIANT,   vr_av);
        put(map_off, ek::DISC,      put_i64(disc));
        put(map_off, ek::PAYLOAD,   pl_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_struct_lit_direct(TypeRef ty,
                                                   std::string_view name,
                                                   const std::vector<std::pair<std::string, lir::LExprPtr>>& fields) {
        auto n_av = put_string(name);
        auto fa   = struct_fields(fields);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::StructLit));
        put(map_off, ek::STRUCT_NAME,  n_av);
        put(map_off, ek::FIELD_NAMES,  fa.names);
        put(map_off, ek::FIELD_VALUES, fa.values);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_call_direct(TypeRef ty,
                                             std::string_view callee,
                                             const std::vector<TypeRef>& type_args,
                                             const std::vector<lir::LExprPtr>& args) {
        auto cn_av = put_string(callee);
        auto ta_av = type_array(type_args);
        auto ar_av = expr_array(args);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::Call));
        put(map_off, ek::CALLEE,    cn_av);
        put(map_off, ek::TYPE_ARGS, ta_av);
        put(map_off, ek::ARGS,      ar_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_method_call_direct(TypeRef ty,
                                                    const lir::LExprPtr& receiver,
                                                    std::string_view method,
                                                    std::string_view resolved_symbol,
                                                    const std::vector<TypeRef>& type_args,
                                                    const std::vector<lir::LExprPtr>& args,
                                                    int32_t vtable_index,
                                                    std::string_view resolved_type,
                                                    std::string_view tag_system,
                                                    std::string_view tag_trait) {
        auto recv_av = expr_av(receiver);
        auto m_av    = put_string(method);
        auto ta_av   = type_array(type_args);
        auto ar_av   = expr_array(args);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::MethodCall));
        put(map_off, ek::RECEIVER,     recv_av);
        put(map_off, ek::METHOD,       m_av);
        put(map_off, ek::TYPE_ARGS,    ta_av);
        put(map_off, ek::ARGS,         ar_av);
        put(map_off, ek::VTABLE_INDEX, put_i32(vtable_index));
        if (!resolved_symbol.empty())
            put(map_off, ek::RESOLVED_SYMBOL, put_string(resolved_symbol));
        if (!resolved_type.empty())
            put(map_off, ek::RESOLVED_TYPE, put_string(resolved_type));
        if (!tag_system.empty())
            put(map_off, ek::TAG_SYSTEM, put_string(tag_system));
        if (!tag_trait.empty())
            put(map_off, ek::TAG_TRAIT, put_string(tag_trait));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_unary_direct(TypeRef ty, std::string_view op,
                                              const lir::LExprPtr& operand) {
        auto op_av = put_string(op);
        auto o_av  = expr_av(operand);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::Unary));
        put(map_off, ek::OP,      op_av);
        put(map_off, ek::OPERAND, o_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_bin_op_direct(TypeRef ty, std::string_view op,
                                               const lir::LExprPtr& lhs,
                                               const lir::LExprPtr& rhs) {
        auto op_av = put_string(op);
        auto l_av  = expr_av(lhs);
        auto r_av  = expr_av(rhs);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::BinOp));
        put(map_off, ek::OP,  op_av);
        put(map_off, ek::LHS, l_av);
        put(map_off, ek::RHS, r_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_field_read_direct(TypeRef ty,
                                                   const lir::LExprPtr& receiver,
                                                   std::string_view field) {
        auto r_av = expr_av(receiver);
        auto f_av = put_string(field);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::FieldRead));
        put(map_off, ek::RECEIVER, r_av);
        put(map_off, ek::NAME,     f_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_index_read_direct(TypeRef ty,
                                                   const lir::LExprPtr& receiver,
                                                   const lir::LExprPtr& index) {
        auto r_av = expr_av(receiver);
        auto i_av = expr_av(index);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::IndexRead));
        put(map_off, ek::RECEIVER, r_av);
        put(map_off, ek::INDEX,    i_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_ptr_arith_direct(TypeRef ty, uint8_t op,
                                                  const lir::LExprPtr& ptr,
                                                  const lir::LExprPtr& offset) {
        auto p_av = expr_av(ptr);
        auto o_av = expr_av(offset);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::PtrArith));
        put(map_off, ek::PTR_ARITH_OP, put_u8(op));
        put(map_off, ek::BASE_PTR,     p_av);
        put(map_off, ek::OFFSET,       o_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_ptr_diff_direct(TypeRef ty, bool by_byte,
                                                 const lir::LExprPtr& lhs,
                                                 const lir::LExprPtr& rhs) {
        auto l_av = expr_av(lhs);
        auto r_av = expr_av(rhs);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::PtrDiff));
        put(map_off, pdk::BY_BYTE, put_bool(by_byte));
        put(map_off, ek::LHS,      l_av);
        put(map_off, ek::RHS,      r_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_if_expr_direct(TypeRef ty,
                                                const lir::LExprPtr& cond,
                                                const lir::LExprPtr& then_val,
                                                const lir::LExprPtr& else_val) {
        auto c_av = expr_av(cond);
        auto t_av = expr_av(then_val);
        auto e_av = expr_av(else_val);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::IfExpr));
        put(map_off, ek::COND,     c_av);
        put(map_off, ek::THEN_VAL, t_av);
        put(map_off, ek::ELSE_VAL, e_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_tuple_lit_direct(TypeRef ty,
                                                  const std::vector<lir::LExprPtr>& elems) {
        auto el_av = expr_array(elems);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::TupleLit));
        put(map_off, ek::ELEMS, el_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_tuple_index_direct(TypeRef ty,
                                                    const lir::LExprPtr& receiver,
                                                    uint32_t index) {
        auto r_av = expr_av(receiver);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::TupleIndex));
        put(map_off, ek::RECEIVER,        r_av);
        put(map_off, ek::TUPLE_INDEX_VAL, put_u32(index));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_arr_lit_direct(TypeRef ty,
                                                const std::vector<lir::LExprPtr>& elems) {
        auto el_av = expr_array(elems);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::ArrLit));
        put(map_off, ek::ELEMS, el_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_block_expr_direct(TypeRef ty,
                                                   const lir::LBlock* block,
                                                   const lir::LExprPtr& result) {
        auto b_av = block_av_raw(block);
        auto r_av = expr_av(result);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::BlockExpr));
        put(map_off, ek::BLOCK,  b_av);
        put(map_off, ek::RESULT, r_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_closure_call_direct(TypeRef ty,
                                                     const lir::LExprPtr& callee,
                                                     const std::vector<lir::LExprPtr>& args) {
        auto c_av = expr_av(callee);
        auto a_av = expr_array(args);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::ClosureCall));
        put(map_off, ek::CALLEE, c_av);
        put(map_off, ek::ARGS,   a_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_fn_ptr_call_direct(TypeRef ty,
                                                    const lir::LExprPtr& callee,
                                                    const std::vector<lir::LExprPtr>& args) {
        auto c_av = expr_av(callee);
        auto a_av = expr_array(args);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::FnPtrCall));
        put(map_off, ek::CALLEE, c_av);
        put(map_off, ek::ARGS,   a_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_match_expr_direct(TypeRef ty,
                                                   const lir::LExprPtr& scrut,
                                                   const std::vector<lir::EMatchArm>& arms) {
        auto sc_av = expr_av(scrut);
        auto ar_av = expr_arm_array(arms);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::MatchExpr));
        put(map_off, ek::SCRUT, sc_av);
        put(map_off, ek::ARMS,  ar_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_format_call_direct(TypeRef ty,
                                                    const lir::LExprPtr& fmt,
                                                    const std::vector<lir::LExprPtr>& args,
                                                    const std::vector<TypeRef>& arg_types) {
        auto f_av  = expr_av(fmt);
        auto a_av  = expr_array(args);
        auto at_av = type_array(arg_types);
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::FormatCall));
        put(map_off, ek::FMT,       f_av);
        put(map_off, ek::ARGS,      a_av);
        put(map_off, ek::ARG_TYPES, at_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    hermes::arena_offset_t emit_closure_box_direct(TypeRef ty,
                                                    const lir::EClosure* inner) {
        // Closure body / captures live inside `inner`; emit_closure walks the
        // sub-tree (block + captures + params) and yields the closure's mirror
        // offset, which we attach as ek::CLOSURE.
        auto cl_av = inner ? closure_av(*inner) : hermes::AnyVal{};
        auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::ClosureBox));
        put(map_off, ek::CLOSURE, cl_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }

    // Stage B.6 — LStmt direct mirror writers. Mirror the per-kind branches of
    // emit_stmt's std::visit but allocate from primitive args, never reading
    // LStmt::kind.
    void put_line(hermes::arena_offset_t map_off, uint32_t line) {
        if (line != 0) put(map_off, sc::LINE, put_u32(line));
    }
    hermes::arena_offset_t emit_let_direct(uint32_t line, std::string_view name,
                                            TypeRef ty, const lir::LExprPtr& value,
                                            bool is_mut) {
        auto name_av = put_string(name);
        auto val_av  = expr_av(value);
        auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::Let));
        put(map_off, sk::NAME,   name_av);
        put(map_off, sk::TYPE,   type_av(ty));
        put(map_off, sk::VALUE,  val_av);
        put(map_off, sk::IS_MUT, put_bool(is_mut));
        put_line(map_off, line);
        return map_off;
    }
    hermes::arena_offset_t emit_assign_direct(uint32_t line, std::string_view name,
                                               const lir::LExprPtr& value) {
        auto name_av = put_string(name);
        auto val_av  = expr_av(value);
        auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::Assign));
        put(map_off, sk::NAME,  name_av);
        put(map_off, sk::VALUE, val_av);
        put_line(map_off, line);
        return map_off;
    }
    hermes::arena_offset_t emit_return_direct(uint32_t line, const lir::LExprPtr& value) {
        auto val_av = expr_av(value);
        auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::Return));
        put(map_off, sk::VALUE, val_av);
        put_line(map_off, line);
        return map_off;
    }
    hermes::arena_offset_t emit_if_stmt_direct(uint32_t line,
                                                const lir::LExprPtr& cond,
                                                const lir::LBlock* then_blk,
                                                const lir::LBlock* else_blk) {
        auto cond_av = expr_av(cond);
        auto then_av = block_av_raw(then_blk);
        hermes::AnyVal else_av;
        if (else_blk) else_av = block_av_raw(else_blk);
        auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::If));
        put(map_off, sk::COND,       cond_av);
        put(map_off, sk::THEN_BLOCK, then_av);
        put(map_off, sk::ELSE_BLOCK, else_av);
        put_line(map_off, line);
        return map_off;
    }
    hermes::arena_offset_t emit_while_direct(uint32_t line,
                                              const lir::LExprPtr& cond,
                                              const lir::LBlock* body,
                                              std::string_view label) {
        auto cond_av = expr_av(cond);
        auto body_av = block_av_raw(body);
        hermes::AnyVal label_av;
        if (!label.empty()) label_av = put_string(label);
        auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::While));
        put(map_off, sk::COND,  cond_av);
        put(map_off, sk::BODY,  body_av);
        put(map_off, sk::LABEL, label_av);
        put_line(map_off, line);
        return map_off;
    }
    hermes::arena_offset_t emit_for_direct(uint32_t line,
                                            std::string_view var,
                                            const lir::LExprPtr& lo,
                                            const lir::LExprPtr& hi,
                                            bool inclusive,
                                            const lir::LBlock* body,
                                            std::string_view label) {
        auto var_av  = put_string(var);
        auto lo_av   = expr_av(lo);
        auto hi_av   = expr_av(hi);
        auto body_av = block_av_raw(body);
        hermes::AnyVal label_av;
        if (!label.empty()) label_av = put_string(label);
        auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::For));
        put(map_off, sk::VAR,       var_av);
        put(map_off, sk::LO,        lo_av);
        put(map_off, sk::HI,        hi_av);
        put(map_off, sk::INCLUSIVE, put_bool(inclusive));
        put(map_off, sk::BODY,      body_av);
        put(map_off, sk::LABEL,     label_av);
        put_line(map_off, line);
        return map_off;
    }
    hermes::arena_offset_t emit_loop_direct(uint32_t line,
                                             const lir::LBlock* body,
                                             std::string_view label,
                                             std::string_view break_slot,
                                             TypeRef result_type) {
        auto body_av = block_av_raw(body);
        hermes::AnyVal label_av, slot_av;
        if (!label.empty())      label_av = put_string(label);
        if (!break_slot.empty()) slot_av  = put_string(break_slot);
        auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::Loop));
        put(map_off, sk::BODY,        body_av);
        put(map_off, sk::LABEL,       label_av);
        put(map_off, sk::BREAK_SLOT,  slot_av);
        put(map_off, sk::RESULT_TYPE, type_av(result_type));
        put_line(map_off, line);
        return map_off;
    }
    hermes::arena_offset_t emit_break_direct(uint32_t line,
                                              const lir::LExprPtr& value,
                                              std::string_view label) {
        auto val_av = expr_av(value);
        hermes::AnyVal label_av;
        if (!label.empty()) label_av = put_string(label);
        auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::Break));
        put(map_off, sk::VALUE, val_av);
        put(map_off, sk::LABEL, label_av);
        put_line(map_off, line);
        return map_off;
    }
    hermes::arena_offset_t emit_continue_direct(uint32_t line,
                                                 std::string_view label) {
        hermes::AnyVal label_av;
        if (!label.empty()) label_av = put_string(label);
        auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::Continue));
        put(map_off, sk::LABEL, label_av);
        put_line(map_off, line);
        return map_off;
    }
    hermes::arena_offset_t emit_block_stmt_direct(uint32_t line,
                                                   const lir::LBlock* body) {
        auto body_av = block_av_raw(body);
        auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::Block));
        put(map_off, sk::BODY, body_av);
        put_line(map_off, line);
        return map_off;
    }
    hermes::arena_offset_t emit_field_write_direct(uint32_t line,
                                                    std::string_view receiver,
                                                    std::string_view field,
                                                    const lir::LExprPtr& value) {
        auto recv_av  = put_string(receiver);
        auto field_av = put_string(field);
        auto val_av   = expr_av(value);
        auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::FieldWrite));
        put(map_off, sk::RECEIVER, recv_av);
        put(map_off, sk::FIELD,    field_av);
        put(map_off, sk::VALUE,    val_av);
        put_line(map_off, line);
        return map_off;
    }
    hermes::arena_offset_t emit_index_write_direct(uint32_t line,
                                                    std::string_view arr,
                                                    const lir::LExprPtr& index,
                                                    const lir::LExprPtr& value) {
        auto arr_av  = put_string(arr);
        auto idx_av  = expr_av(index);
        auto val_av  = expr_av(value);
        auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::IndexWrite));
        put(map_off, sk::NAME,  arr_av);
        put(map_off, sk::INDEX, idx_av);
        put(map_off, sk::VALUE, val_av);
        put_line(map_off, line);
        return map_off;
    }
    hermes::arena_offset_t emit_field_index_write_direct(uint32_t line,
                                                          std::string_view receiver,
                                                          std::string_view field,
                                                          const lir::LExprPtr& index,
                                                          const lir::LExprPtr& value) {
        auto recv_av  = put_string(receiver);
        auto field_av = put_string(field);
        auto idx_av   = expr_av(index);
        auto val_av   = expr_av(value);
        auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::FieldIndexWrite));
        put(map_off, sk::RECEIVER, recv_av);
        put(map_off, sk::FIELD,    field_av);
        put(map_off, sk::INDEX,    idx_av);
        put(map_off, sk::VALUE,    val_av);
        put_line(map_off, line);
        return map_off;
    }
    hermes::arena_offset_t emit_expr_stmt_direct(uint32_t line,
                                                  const lir::LExprPtr& expr) {
        auto expr_avv = expr_av(expr);
        auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::ExprStmt));
        put(map_off, sk::EXPR, expr_avv);
        put_line(map_off, line);
        return map_off;
    }
    hermes::arena_offset_t emit_match_stmt_direct(uint32_t line,
                                                   const lir::LExprPtr& scrut,
                                                   const std::vector<lir::LMatchArm>& arms) {
        auto scrut_av = expr_av(scrut);
        auto arms_av  = arm_array(arms);
        auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::Match));
        put(map_off, sk::SCRUT, scrut_av);
        put(map_off, sk::ARMS,  arms_av);
        put_line(map_off, line);
        return map_off;
    }
    hermes::arena_offset_t emit_for_each_direct(uint32_t line,
                                                 std::string_view var,
                                                 const lir::LExprPtr& iter,
                                                 TypeRef elem_type,
                                                 int64_t arr_size,
                                                 bool is_slice,
                                                 const lir::LBlock* body) {
        auto var_av  = put_string(var);
        auto iter_av = expr_av(iter);
        auto body_av = block_av_raw(body);
        auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::ForEach));
        put(map_off, sk::VAR,       var_av);
        put(map_off, sk::ITER,      iter_av);
        put(map_off, sk::ELEM_TYPE, type_av(elem_type));
        put(map_off, sk::ARR_SIZE,  put_i64(arr_size));
        put(map_off, sk::IS_SLICE,  put_bool(is_slice));
        put(map_off, sk::BODY,      body_av);
        put_line(map_off, line);
        return map_off;
    }
    hermes::arena_offset_t emit_deref_write_direct(uint32_t line,
                                                    const lir::LExprPtr& ptr,
                                                    const lir::LExprPtr& value) {
        auto ptr_av = expr_av(ptr);
        auto val_av = expr_av(value);
        auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::DerefWrite));
        put(map_off, sk::PTR,   ptr_av);
        put(map_off, sk::VALUE, val_av);
        put_line(map_off, line);
        return map_off;
    }
    hermes::arena_offset_t emit_drop_direct(uint32_t line,
                                             std::string_view var_name,
                                             std::string_view drop_fn,
                                             TypeRef ty,
                                             bool drop_fields,
                                             const std::vector<std::string>& moved_fields) {
        auto var_av = put_string(var_name);
        hermes::AnyVal drop_av;
        if (!drop_fn.empty()) drop_av = put_string(drop_fn);
        hermes::AnyVal moved_av;
        if (!moved_fields.empty()) moved_av = string_array(moved_fields);
        auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::Drop));
        put(map_off, sk::NAME,         var_av);
        put(map_off, sk::DROP_FN,      drop_av);
        put(map_off, sk::TYPE,         type_av(ty));
        put(map_off, sk::DROP_FIELDS,  put_bool(drop_fields));
        if (!moved_fields.empty())
            put(map_off, sk::MOVED_FIELDS, moved_av);
        put_line(map_off, line);
        return map_off;
    }
    hermes::arena_offset_t emit_deref_field_write_direct(uint32_t line,
                                                          std::string_view receiver,
                                                          std::string_view type_name,
                                                          std::string_view field,
                                                          const lir::LExprPtr& value) {
        auto recv_av  = put_string(receiver);
        auto type_avv = put_string(type_name);
        auto field_av = put_string(field);
        auto val_av   = expr_av(value);
        auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::DerefFieldWrite));
        put(map_off, sk::RECEIVER,   recv_av);
        put(map_off, sk::TYPE_NAME,  type_avv);
        put(map_off, sk::FIELD,      field_av);
        put(map_off, sk::VALUE,      val_av);
        put_line(map_off, line);
        return map_off;
    }
    hermes::arena_offset_t emit_tuple_write_direct(uint32_t line,
                                                    std::string_view receiver,
                                                    uint32_t index,
                                                    const lir::LExprPtr& value,
                                                    TypeRef recv_type) {
        auto recv_av = put_string(receiver);
        auto val_av  = expr_av(value);
        auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::TupleWrite));
        put(map_off, sk::RECEIVER,        recv_av);
        put(map_off, sk::TUPLE_INDEX_VAL, put_u32(index));
        put(map_off, sk::VALUE,           val_av);
        put(map_off, sk::RECV_TYPE,       type_av(recv_type));
        put_line(map_off, line);
        return map_off;
    }
    hermes::arena_offset_t emit_let_else_direct(uint32_t line,
                                                 const lir::Pattern& pat,
                                                 const lir::LExprPtr& scrut,
                                                 const lir::LBlock* else_block,
                                                 const std::vector<lir::LExprPtr>& guards) {
        auto pat_off  = emit_pat(pat);
        auto scrut_av = expr_av(scrut);
        auto eb_av    = block_av_raw(else_block);
        auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::LetElse));
        put(map_off, sk::PAT,           hermes::AnyVal::from_offset(pat_off));
        put(map_off, sk::SCRUT,         scrut_av);
        put(map_off, sk::ELSE_DIVERGE,  eb_av);
        if (!guards.empty())
            put(map_off, sk::LET_ELSE_GUARDS, expr_array(guards));   // G161-3
        put_line(map_off, line);
        return map_off;
    }
    hermes::arena_offset_t emit_chain_field_write_direct(uint32_t line,
                                                          std::string_view receiver,
                                                          std::string_view mid_field,
                                                          const std::vector<std::string>& extras,
                                                          std::string_view field,
                                                          const lir::LExprPtr& value) {
        auto recv_av   = put_string(receiver);
        auto mid_av    = put_string(mid_field);
        auto extras_av = string_array(extras);
        auto fld_av    = put_string(field);
        auto val_av    = expr_av(value);
        auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::ChainFieldWrite));
        put(map_off, sk::RECEIVER,   recv_av);
        put(map_off, sk::MID_FIELD,  mid_av);
        if (!extras.empty())
            put(map_off, sk::EXTRA_MIDS, extras_av);
        put(map_off, sk::FIELD,      fld_av);
        put(map_off, sk::VALUE,      val_av);
        put_line(map_off, line);
        return map_off;
    }

    // ── Stage B.6 — HermesVal direct mirror writers ─────────────────────────
    // Each mirrors the corresponding branch of emit_hv's std::visit but
    // allocates from primitive args, never reading HermesVal::kind. Children
    // (HermesValPtr) must already have their own mirror_offset_; hv_av will
    // back-fill the cache via the field-as-truth path.
    static constexpr int32_t HV_BASE_DIRECT = 200;

    hermes::arena_offset_t emit_hv_null_direct() {
        return make_map(hermes::schema::lir_expr(HV_BASE_DIRECT + 0));
    }
    hermes::arena_offset_t emit_hv_bool_direct(bool value) {
        auto map_off = make_map(hermes::schema::lir_expr(HV_BASE_DIRECT + 1));
        put(map_off, hk::BOOL_VALUE, put_bool(value));
        return map_off;
    }
    hermes::arena_offset_t emit_hv_int_direct(int64_t value) {
        auto map_off = make_map(hermes::schema::lir_expr(HV_BASE_DIRECT + 2));
        put(map_off, hk::INT_VALUE, put_i64(value));
        return map_off;
    }
    hermes::arena_offset_t emit_hv_float_direct(double value) {
        auto map_off = make_map(hermes::schema::lir_expr(HV_BASE_DIRECT + 3));
        put(map_off, hk::FLOAT_VALUE, put_f64(value));
        return map_off;
    }
    hermes::arena_offset_t emit_hv_str_direct(std::string_view value) {
        auto s_av = put_string(value);
        auto map_off = make_map(hermes::schema::lir_expr(HV_BASE_DIRECT + 4));
        put(map_off, hk::STR_VALUE, s_av);
        return map_off;
    }
    hermes::arena_offset_t emit_hv_map_direct(const std::vector<lir::HVMapEntry>& entries,
                                               std::string_view key_type) {
        std::vector<hermes::AnyVal> key_strs, key_ints, val_avs;
        key_strs.reserve(entries.size());
        key_ints.reserve(entries.size());
        val_avs.reserve(entries.size());
        for (auto& e : entries) {
            if (std::holds_alternative<std::string>(e.key))
                key_strs.push_back(put_string(std::get<std::string>(e.key)));
            else
                key_ints.push_back(put_i64(std::get<int64_t>(e.key)));
            val_avs.push_back(hv_av(e.val));
        }
        hermes::AnyVal keys_av;
        if (!key_strs.empty()) {
            auto off = make_array(key_strs.size());
            for (auto av : key_strs) array_push(off, av);
            keys_av = hermes::AnyVal::from_offset(off);
        } else if (!key_ints.empty()) {
            auto off = make_array(key_ints.size());
            for (auto av : key_ints) array_push(off, av);
            keys_av = hermes::AnyVal::from_offset(off);
        }
        hermes::AnyVal vals_av;
        if (!val_avs.empty()) {
            auto off = make_array(val_avs.size());
            for (auto av : val_avs) array_push(off, av);
            vals_av = hermes::AnyVal::from_offset(off);
        }
        auto map_off = make_map(hermes::schema::lir_expr(HV_BASE_DIRECT + 5));
        put(map_off, hk::MAP_KEYS,   keys_av);
        put(map_off, hk::MAP_VALUES, vals_av);
        if (!key_type.empty())
            put(map_off, hk::TYPE_NAME, put_string(key_type));
        return map_off;
    }
    hermes::arena_offset_t emit_hv_array_direct(const std::vector<lir::HermesValPtr>& elements,
                                                 std::string_view elem_type) {
        std::vector<hermes::AnyVal> elems;
        elems.reserve(elements.size());
        for (auto& e : elements) elems.push_back(hv_av(e));
        hermes::AnyVal arr_av;
        if (!elems.empty()) {
            auto off = make_array(elems.size());
            for (auto av : elems) array_push(off, av);
            arr_av = hermes::AnyVal::from_offset(off);
        }
        auto map_off = make_map(hermes::schema::lir_expr(HV_BASE_DIRECT + 6));
        put(map_off, hk::ELEMS, arr_av);
        if (!elem_type.empty())
            put(map_off, hk::TYPE_NAME, put_string(elem_type));
        return map_off;
    }
    hermes::arena_offset_t emit_hv_capture_direct(uint32_t param_index, uint32_t value_index) {
        auto map_off = make_map(hermes::schema::lir_expr(HV_BASE_DIRECT + 7));
        put(map_off, hk::PARAM_INDEX, put_u32(param_index));
        put(map_off, hk::VALUE_INDEX, put_u32(value_index));
        return map_off;
    }
    hermes::arena_offset_t emit_hv_type_direct(uint32_t kind, uint64_t uid, std::string_view name) {
        auto name_av = put_string(name);
        auto map_off = make_map(hermes::schema::lir_expr(HV_BASE_DIRECT + 8));
        put(map_off, hk::STR_VALUE, name_av);
        put(map_off, hk::TYPE_KIND, put_u32(kind));
        put(map_off, hk::TYPE_UID,  put_u64(uid));
        return map_off;
    }

    // Stage B.6 — Pattern direct mirror writers. Mirror the per-kind branches
    // of emit_pat's std::visit but allocate from primitive args, never reading
    // Pattern::kind. Sub-pattern children must already carry their own
    // mirror_offset_; pat_array recurses through the cache-hit fast path.
    hermes::arena_offset_t emit_pat_variant_direct(std::string_view enum_name,
                                                    std::string_view variant,
                                                    int64_t disc) {
        auto enum_av    = put_string(enum_name);
        auto variant_av = put_string(variant);
        auto map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::Variant));
        put(map_off, pk::ENUM_NAME, enum_av);
        put(map_off, pk::VARIANT,   variant_av);
        put(map_off, pk::DISC,      put_i64(disc));
        return map_off;
    }
    hermes::arena_offset_t emit_pat_int_direct(int64_t value) {
        auto map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::Int));
        put(map_off, pk::INT_VALUE, put_i64(value));
        return map_off;
    }
    hermes::arena_offset_t emit_pat_bool_direct(bool value) {
        auto map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::Bool));
        put(map_off, pk::BOOL_VALUE, put_bool(value));
        return map_off;
    }
    hermes::arena_offset_t emit_pat_wild_direct(std::string_view name) {
        auto name_av = name.empty() ? hermes::AnyVal{} : put_string(name);
        auto map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::Wild));
        put(map_off, pk::NAME, name_av);
        return map_off;
    }
    hermes::arena_offset_t emit_pat_variant_data_direct(std::string_view enum_name,
                                                         std::string_view variant,
                                                         int64_t disc,
                                                         const std::vector<std::string>& bindings,
                                                         const std::vector<TypeRef>& binding_types) {
        auto enum_av     = put_string(enum_name);
        auto variant_av  = put_string(variant);
        auto bindings_av = string_array(bindings);
        auto btypes_av   = type_array(binding_types);
        auto map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::VariantData));
        put(map_off, pk::ENUM_NAME,      enum_av);
        put(map_off, pk::VARIANT,        variant_av);
        put(map_off, pk::DISC,           put_i64(disc));
        put(map_off, pk::BINDINGS,       bindings_av);
        put(map_off, pk::BINDING_TYPES,  btypes_av);
        return map_off;
    }
    hermes::arena_offset_t emit_pat_or_direct(const std::vector<lir::Pattern>& alts) {
        auto subs_av = pat_array(alts);
        auto map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::Or));
        put(map_off, pk::SUBS, subs_av);
        return map_off;
    }
    hermes::arena_offset_t emit_pat_tuple_direct(const std::vector<std::string>& bindings,
                                                  const std::vector<TypeRef>& binding_types,
                                                  const std::vector<lir::Pattern>& subs) {
        auto bindings_av = string_array(bindings);
        auto btypes_av   = type_array(binding_types);
        auto subs_av     = pat_array(subs);
        auto map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::Tuple));
        put(map_off, pk::BINDINGS,      bindings_av);
        put(map_off, pk::BINDING_TYPES, btypes_av);
        put(map_off, pk::SUBS,          subs_av);
        return map_off;
    }
    hermes::arena_offset_t emit_pat_range_direct(int64_t lo, int64_t hi) {
        auto map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::Range));
        put(map_off, pk::LO, put_i64(lo));
        put(map_off, pk::HI, put_i64(hi));
        return map_off;
    }
    hermes::arena_offset_t emit_pat_struct_direct(std::string_view struct_name,
                                                   const std::vector<lir::PatFieldBinding>& fields,
                                                   bool has_rest) {
        auto name_av   = put_string(struct_name);
        auto fields_av = field_binding_array(fields);
        auto map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::Struct));
        put(map_off, pk::STRUCT_NAME, name_av);
        put(map_off, pk::FIELDS,      fields_av);
        put(map_off, pk::HAS_REST,    put_bool(has_rest));
        return map_off;
    }
    hermes::arena_offset_t emit_pat_slice_direct(const std::vector<lir::Pattern>& prefix,
                                                  const std::vector<lir::Pattern>& rest,
                                                  const std::vector<lir::Pattern>& suffix) {
        auto pre_av  = pat_array(prefix);
        auto rest_av = pat_array(rest);
        auto suf_av  = pat_array(suffix);
        auto map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::Slice));
        put(map_off, pk::PREFIX, pre_av);
        put(map_off, pk::REST,   rest_av);
        put(map_off, pk::SUFFIX, suf_av);
        return map_off;
    }
    hermes::arena_offset_t emit_pat_at_direct(std::string_view name,
                                               const std::vector<lir::Pattern>& sub,
                                               TypeRef type) {
        auto name_av = put_string(name);
        auto sub_av  = pat_array(sub);
        auto map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::At));
        put(map_off, pk::NAME, name_av);
        put(map_off, pk::SUB,  sub_av);
        put(map_off, pk::TYPE, type_av(type));
        return map_off;
    }
    hermes::arena_offset_t emit_pat_ref_bind_direct(std::string_view name,
                                                     bool is_mut,
                                                     TypeRef bind_type) {
        auto name_av = put_string(name);
        auto map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::RefBind));
        put(map_off, pk::NAME,      name_av);
        put(map_off, pk::IS_MUT,    put_bool(is_mut));
        put(map_off, pk::BIND_TYPE, type_av(bind_type));
        return map_off;
    }
    hermes::arena_offset_t emit_pat_ref_pat_direct(const std::vector<lir::Pattern>& inner,
                                                    bool is_mut) {
        auto inner_av = pat_array(inner);
        auto map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::RefPat));
        put(map_off, pk::INNER,  inner_av);
        put(map_off, pk::IS_MUT, put_bool(is_mut));
        return map_off;
    }

private:
    // ── primitive helpers ───────────────────────────────────────────────────

    hermes::arena_offset_t offset_of(const void* p) const noexcept {
        auto off = static_cast<uint32_t>(
            static_cast<const uint8_t*>(p) - arena_.head().data());
        return hermes::arena_offset_t{off};
    }

    hermes::TinyObjectMap* tom_at(hermes::arena_offset_t off) noexcept {
        return reinterpret_cast<hermes::TinyObjectMap*>(
            arena_.head().data() + off.value());
    }
    hermes::ObjectArray* arr_at(hermes::arena_offset_t off) noexcept {
        return reinterpret_cast<hermes::ObjectArray*>(
            arena_.head().data() + off.value());
    }

    hermes::AnyVal put_string(std::string_view s) {
        auto p = hermes::ArenaString::create(arena_, s);
        LOGOS_ASSERT(p.has_value(), "LIR-MIRROR-001", "ArenaString alloc failed");
        return hermes::AnyVal::from_offset(offset_of(*p));
    }
    hermes::AnyVal put_i64(int64_t v) {
        auto av = hermes::anyval_put<int64_t>(arena_, v);
        LOGOS_ASSERT(av.has_value(), "LIR-MIRROR-002", "i64 anyval put failed");
        return *av;
    }
    hermes::AnyVal put_u64(uint64_t v) {
        auto av = hermes::anyval_put<uint64_t>(arena_, v);
        LOGOS_ASSERT(av.has_value(), "LIR-MIRROR-002", "u64 anyval put failed");
        return *av;
    }
    hermes::AnyVal put_f64(double v) {
        auto av = hermes::anyval_put<double>(arena_, v);
        LOGOS_ASSERT(av.has_value(), "LIR-MIRROR-002", "f64 anyval put failed");
        return *av;
    }
    hermes::AnyVal put_bool(bool v) {
        return hermes::AnyVal::from_value<uint8_t>(
            v ? 1 : 0, hermes::type_hash::Bool);
    }
    hermes::AnyVal put_i32(int32_t v) {
        return hermes::AnyVal::from_value<int32_t>(v);
    }
    hermes::AnyVal put_u32(uint32_t v) {
        return hermes::AnyVal::from_value<uint32_t>(v);
    }
    hermes::AnyVal put_u8(uint8_t v) {
        return hermes::AnyVal::from_value<uint8_t>(v);
    }

    hermes::AnyVal type_av(TypeRef t) {
        if (!t) return hermes::AnyVal{};
        // Phase 5.B step 3: foreign TypeRef → intern into local pool before
        // taking its offset. The mirror's TYPE field is interpreted in the
        // local arena, so a foreign offset here would read garbage later.
        // is_external() is a single uint32 compare on the hot local path.
        if (pool_ && t.is_external()) t = pool_->intern_foreign(t);
        return hermes::AnyVal::from_offset(t.offset());
    }

    // ── child-emit helpers (returns AnyVal pointing at child mirror) ───────

    hermes::AnyVal expr_av(const lir::LExprPtr& e) {
        if (!e) return hermes::AnyVal{};
        return hermes::AnyVal::from_offset(emit_expr(*e));
    }
    hermes::AnyVal stmt_av(const LStmt& s) {
        return hermes::AnyVal::from_offset(emit_stmt(s));
    }
    hermes::AnyVal block_av(const lir::LBlockPtr& b) {
        if (!b) return hermes::AnyVal{};
        return hermes::AnyVal::from_offset(emit_block(*b));
    }
    hermes::AnyVal block_av_raw(const LBlock* b) {
        if (!b) return hermes::AnyVal{};
        return hermes::AnyVal::from_offset(emit_block(*b));
    }
    hermes::AnyVal pat_av(const Pattern& p) {
        return hermes::AnyVal::from_offset(emit_pat(p));
    }
    hermes::AnyVal hv_av(const lir::HermesValPtr& v) {
        if (!v) return hermes::AnyVal{};
        return hermes::AnyVal::from_offset(emit_hv(*v));
    }
    hermes::AnyVal arm_av(const LMatchArm& a) {
        return hermes::AnyVal::from_offset(emit_arm(a));
    }
    hermes::AnyVal closure_av(const EClosure& c) {
        return hermes::AnyVal::from_offset(emit_closure(c));
    }

    // ── ObjectArray helpers ────────────────────────────────────────────────

    hermes::arena_offset_t make_array(size_t n) {
        if (dry_run_) return hermes::arena_offset_t{};
        auto arr = hermes::ObjectArray::create(arena_, n == 0 ? 1 : n);
        LOGOS_ASSERT(arr.has_value(), "LIR-MIRROR-003", "ObjectArray alloc failed");
        return offset_of(*arr);
    }
    void array_push(hermes::arena_offset_t arr_off, hermes::AnyVal v) {
        if (dry_run_) return;
        auto r = arr_at(arr_off)->push_back(v, arena_);
        LOGOS_ASSERT(r.has_value(), "LIR-MIRROR-003", "ObjectArray push failed");
    }

    hermes::AnyVal expr_array(const std::vector<lir::LExprPtr>& v) {
        if (v.empty()) return hermes::AnyVal{};
        std::vector<hermes::AnyVal> elems;
        elems.reserve(v.size());
        for (auto& e : v) elems.push_back(expr_av(e));
        auto arr_off = make_array(elems.size());
        for (auto av : elems) array_push(arr_off, av);
        return hermes::AnyVal::from_offset(arr_off);
    }
    hermes::AnyVal type_array(const std::vector<TypeRef>& v) {
        if (v.empty()) return hermes::AnyVal{};
        auto arr_off = make_array(v.size());
        for (auto t : v) array_push(arr_off, type_av(t));
        return hermes::AnyVal::from_offset(arr_off);
    }
    hermes::AnyVal string_array(const std::vector<std::string>& v) {
        if (v.empty()) return hermes::AnyVal{};
        std::vector<hermes::AnyVal> elems;
        elems.reserve(v.size());
        for (auto& s : v) elems.push_back(put_string(s));
        auto arr_off = make_array(elems.size());
        for (auto av : elems) array_push(arr_off, av);
        return hermes::AnyVal::from_offset(arr_off);
    }
    hermes::AnyVal pat_array(const std::vector<Pattern>& v) {
        if (v.empty()) return hermes::AnyVal{};
        std::vector<hermes::AnyVal> elems;
        elems.reserve(v.size());
        for (auto& p : v) elems.push_back(pat_av(p));
        auto arr_off = make_array(elems.size());
        for (auto av : elems) array_push(arr_off, av);
        return hermes::AnyVal::from_offset(arr_off);
    }
    hermes::AnyVal u32_array(const std::vector<uint32_t>& v) {
        if (v.empty()) return hermes::AnyVal{};
        std::vector<hermes::AnyVal> elems;
        elems.reserve(v.size());
        for (auto x : v) elems.push_back(put_u32(x));
        auto arr_off = make_array(elems.size());
        for (auto av : elems) array_push(arr_off, av);
        return hermes::AnyVal::from_offset(arr_off);
    }
    hermes::AnyVal arm_array(const std::vector<LMatchArm>& v) {
        if (v.empty()) return hermes::AnyVal{};
        std::vector<hermes::AnyVal> elems;
        elems.reserve(v.size());
        for (auto& a : v) elems.push_back(arm_av(a));
        auto arr_off = make_array(elems.size());
        for (auto av : elems) array_push(arr_off, av);
        return hermes::AnyVal::from_offset(arr_off);
    }

    // EMatchExpr arms have a different shape than LMatchArm (value vs body) —
    // emit each as a small TinyObjectMap and return an array of AnyVal.
    hermes::AnyVal expr_arm_array(const std::vector<lir::EMatchArm>& v);

    // EStructLit fields: emit FIELD_NAMES + FIELD_VALUES parallel arrays
    // and write them to the parent map. Returns the two arrays as a pair.
    struct FieldArrays {
        hermes::AnyVal names;
        hermes::AnyVal values;
    };
    FieldArrays struct_fields(
        const std::vector<std::pair<std::string, lir::LExprPtr>>& fields);

    // PatFieldBinding array (for PatStruct).
    hermes::AnyVal field_binding_array(
        const std::vector<lir::PatFieldBinding>& v);

    // ── map creation + put helpers ─────────────────────────────────────────

    hermes::arena_offset_t make_map(uint64_t schema_code, uint64_t cap = 8) {
        if (dry_run_) return hermes::arena_offset_t{};
        auto m = hermes::TinyObjectMap::create(arena_, cap);
        LOGOS_ASSERT(m.has_value(), "LIR-MIRROR-004",
            "TinyObjectMap allocation failed");
        auto off = offset_of(*m);
        (*m)->set_schema_type_code(schema_code);
        return off;
    }
    void put(hermes::arena_offset_t map_off,
             const lir_schema::Key& key, hermes::AnyVal val) {
        if (dry_run_) return;
        if (val.is_null()) return;
        auto r = tom_at(map_off)->put(key.code, val, arena_);
        LOGOS_ASSERT(r.has_value(), "LIR-MIRROR-005",
            "TinyObjectMap put failed");
    }

    // ── expression emit ────────────────────────────────────────────────────

    hermes::arena_offset_t emit_expr(const LExpr& e);
    hermes::arena_offset_t emit_stmt(const LStmt& s);
    hermes::arena_offset_t emit_block(const LBlock& b);
    hermes::arena_offset_t emit_pat(const Pattern& p);
    hermes::arena_offset_t emit_hv(const HermesVal& v);
    hermes::arena_offset_t emit_arm(const LMatchArm& a);
    hermes::arena_offset_t emit_closure(const EClosure& c);
    hermes::arena_offset_t emit_expr_arm(const lir::EMatchArm& a);
    hermes::arena_offset_t emit_field_binding(const lir::PatFieldBinding& fb);
};

// ──────────────────────────────────────────────────────────────────────────
// Block / function body
// ──────────────────────────────────────────────────────────────────────────

hermes::arena_offset_t LirMirrorEmitter::emit_block(const LBlock& b) {
    bool backfill_only = false;
    if (b.mirror_offset_ != hermes::arena_offset_t{}) {
        if (auto it = table_.block.find(&b); it != table_.block.end()) {
            return it->second;
        }
        table_.block[&b] = b.mirror_offset_;
        table_.block_by_offset[b.mirror_offset_.value()] = &b;
        backfill_only = true;
    } else if (auto it = table_.block.find(&b); it != table_.block.end()) {
        // Heap-address recycling: stale cache entry for a freed LBlock.
        // Invalidate and fall through to emit a fresh mirror.
        table_.block_by_offset.erase(it->second.value());
        table_.block.erase(it);
    }

    bool save_dry = dry_run_;
    if (backfill_only) dry_run_ = true;

    // Pre-emit statements so child offsets exist before we create the array.
    std::vector<hermes::AnyVal> stmt_elems;
    stmt_elems.reserve(b.stmts.size());
    for (auto& s : b.stmts) stmt_elems.push_back(stmt_av(s));

    hermes::AnyVal stmts_av;
    if (!stmt_elems.empty()) {
        auto arr_off = make_array(stmt_elems.size());
        for (auto av : stmt_elems) array_push(arr_off, av);
        stmts_av = hermes::AnyVal::from_offset(arr_off);
    }

    // Block uses lir_stmt category with a synthetic "Count" code (== stmt::Count)
    // — out-of-band of real stmt codes — to keep the category space simple.
    auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Count));
    if (!stmts_av.is_null()) put(map_off, sk::ARMS, stmts_av);  // reuse ARMS key as STMTS list

    dry_run_ = save_dry;
    if (backfill_only) return b.mirror_offset_;

    b.mirror_offset_ = map_off;
    table_.block[&b] = map_off;
    table_.block_by_offset[map_off.value()] = &b;
    return map_off;
}

// ──────────────────────────────────────────────────────────────────────────
// LMatchArm / EMatchArm / PatFieldBinding / EClosure
// ──────────────────────────────────────────────────────────────────────────

hermes::arena_offset_t LirMirrorEmitter::emit_arm(const LMatchArm& a) {
    auto pat_off    = emit_pat(a.pat);
    auto body_off   = emit_block(*a.body);
    hermes::AnyVal guard_av;
    if (a.guard.has_value()) guard_av = expr_av(*a.guard);

    auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Count + 1));
    put(map_off, ak::PAT,   hermes::AnyVal::from_offset(pat_off));
    put(map_off, ak::BODY,  hermes::AnyVal::from_offset(body_off));
    put(map_off, ak::GUARD, guard_av);
    return map_off;
}

hermes::arena_offset_t LirMirrorEmitter::emit_expr_arm(const lir::EMatchArm& a) {
    auto pat_off    = emit_pat(a.pat);
    auto value_off  = emit_expr(*a.value);
    hermes::AnyVal guard_av;
    if (a.guard.has_value()) guard_av = expr_av(*a.guard);

    auto map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Count + 2));
    put(map_off, ak::PAT,   hermes::AnyVal::from_offset(pat_off));
    put(map_off, ak::VALUE, hermes::AnyVal::from_offset(value_off));
    put(map_off, ak::GUARD, guard_av);
    return map_off;
}

hermes::AnyVal LirMirrorEmitter::expr_arm_array(
    const std::vector<lir::EMatchArm>& v)
{
    if (v.empty()) return hermes::AnyVal{};
    std::vector<hermes::AnyVal> elems;
    elems.reserve(v.size());
    for (auto& a : v) elems.push_back(
        hermes::AnyVal::from_offset(emit_expr_arm(a)));
    auto arr_off = make_array(elems.size());
    for (auto av : elems) array_push(arr_off, av);
    return hermes::AnyVal::from_offset(arr_off);
}

hermes::arena_offset_t LirMirrorEmitter::emit_field_binding(
    const lir::PatFieldBinding& fb)
{
    auto name_av = put_string(fb.field_name);
    auto subs_av = pat_array(fb.sub);

    auto map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Count));
    put(map_off, pk::FIELD_NAME, name_av);
    put(map_off, pk::SUB,        subs_av);
    return map_off;
}

hermes::AnyVal LirMirrorEmitter::field_binding_array(
    const std::vector<lir::PatFieldBinding>& v)
{
    if (v.empty()) return hermes::AnyVal{};
    std::vector<hermes::AnyVal> elems;
    elems.reserve(v.size());
    for (auto& fb : v) elems.push_back(
        hermes::AnyVal::from_offset(emit_field_binding(fb)));
    auto arr_off = make_array(elems.size());
    for (auto av : elems) array_push(arr_off, av);
    return hermes::AnyVal::from_offset(arr_off);
}

LirMirrorEmitter::FieldArrays LirMirrorEmitter::struct_fields(
    const std::vector<std::pair<std::string, lir::LExprPtr>>& fields)
{
    FieldArrays out;
    if (fields.empty()) return out;

    std::vector<hermes::AnyVal> name_elems, value_elems;
    name_elems.reserve(fields.size());
    value_elems.reserve(fields.size());
    for (auto& [n, v] : fields) {
        name_elems.push_back(put_string(n));
        value_elems.push_back(expr_av(v));
    }
    auto names_off = make_array(name_elems.size());
    for (auto av : name_elems) array_push(names_off, av);
    auto values_off = make_array(value_elems.size());
    for (auto av : value_elems) array_push(values_off, av);
    out.names  = hermes::AnyVal::from_offset(names_off);
    out.values = hermes::AnyVal::from_offset(values_off);
    return out;
}

hermes::arena_offset_t LirMirrorEmitter::emit_closure(const EClosure& c) {
    // Body first
    auto body_off = emit_block(c.body);

    // Capture types as type-array
    auto cap_types_av = type_array(c.capture_types);
    auto captures_av  = string_array(c.captures);

    // Param names + types as parallel arrays.
    hermes::AnyVal param_names_av, param_types_av;
    if (!c.params.empty()) {
        std::vector<hermes::AnyVal> n_elems, t_elems;
        n_elems.reserve(c.params.size());
        t_elems.reserve(c.params.size());
        for (auto& p : c.params) {
            n_elems.push_back(put_string(p.name));
            t_elems.push_back(p.type ? type_av(p.type) : hermes::AnyVal{});
        }
        auto n_off = make_array(n_elems.size());
        for (auto av : n_elems) array_push(n_off, av);
        auto t_off = make_array(t_elems.size());
        for (auto av : t_elems) array_push(t_off, av);
        param_names_av = hermes::AnyVal::from_offset(n_off);
        param_types_av = hermes::AnyVal::from_offset(t_off);
    }

    // 10 keys (block, name, cap-types, cap-names, param-names, param-types,
    // ret-type, is-move, as-fn-ptr, mut-captures) — default cap=8 overflows.
    auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::ClosureBox)
                            | (1ULL << 47),
                            /*cap=*/12);
    put(map_off, ck::BLOCK,         hermes::AnyVal::from_offset(body_off));
    if (!c.closure_id.empty())
        put(map_off, ck::NAME, put_string(c.closure_id));
    put(map_off, ck::CAPTURE_TYPES, cap_types_av);
    put(map_off, ck::CAPTURE_NAMES, captures_av);
    put(map_off, ck::PARAM_NAMES,   param_names_av);
    put(map_off, ck::PARAM_TYPES,   param_types_av);
    if (c.ret_type) put(map_off, ck::RET_TYPE, type_av(c.ret_type));
    put(map_off, ck::IS_MOVE,   put_bool(c.is_move));
    put(map_off, ck::AS_FN_PTR, put_bool(c.as_fn_ptr));
    if (c.escapes) put(map_off, ck::ESCAPES, put_bool(c.escapes));
    // C5-cl-08: per-capture mut flag — emit as parallel Array<u8> only when
    // at least one capture is mutated, so untouched closures keep the
    // existing schema footprint.
    bool any_mut = false;
    for (auto m : c.mut_captures) if (m) { any_mut = true; break; }
    if (any_mut && c.mut_captures.size() == c.captures.size()) {
        std::vector<hermes::AnyVal> m_elems;
        m_elems.reserve(c.mut_captures.size());
        for (bool m : c.mut_captures) m_elems.push_back(put_bool(m));
        auto m_off = make_array(m_elems.size());
        for (auto av : m_elems) array_push(m_off, av);
        put(map_off, ck::MUT_CAPTURES, hermes::AnyVal::from_offset(m_off));
    }
    return map_off;
}

// ──────────────────────────────────────────────────────────────────────────
// HermesVal mirror
// ──────────────────────────────────────────────────────────────────────────

hermes::arena_offset_t LirMirrorEmitter::emit_hv(const HermesVal& v) {
    // B.6 Stage 3.5 step 6: mirror_offset_ is field-as-truth. All HermesVal
    // construction sites (sema alloc_hv_emit, mono clone_hv) eagerly emit
    // and set mirror_offset_ via per-kind direct emitters; nested children
    // are registered transitively. The bulk std::visit fallback is now
    // unreachable.
    LOGOS_ASSERT(v.mirror_offset_ != hermes::arena_offset_t{},
                 "B6.S35.S6",
                 "emit_hv: HermesVal reached without mirror_offset_ set "
                 "(construction site missed direct-emit migration)");
    if (auto it = table_.hermes_val.find(&v); it != table_.hermes_val.end()) {
        if (it->second == v.mirror_offset_) return it->second;
        table_.hermes_val_by_offset.erase(uint32_t(it->second.value()));
        it->second = v.mirror_offset_;
        table_.hermes_val_by_offset[v.mirror_offset_.value()] = &v;
        return v.mirror_offset_;
    }
    table_.hermes_val[&v] = v.mirror_offset_;
    table_.hermes_val_by_offset[v.mirror_offset_.value()] = &v;
    return v.mirror_offset_;
}


// ──────────────────────────────────────────────────────────────────────────
// Pattern mirror
// ──────────────────────────────────────────────────────────────────────────

hermes::arena_offset_t LirMirrorEmitter::emit_pat(const Pattern& p) {
    // B.6 Stage 3.5 step 3: mirror_offset_ is now field-as-truth. All
    // Pattern construction sites (sema build_pattern_impl, sema
    // make_pat_wild, mono PatSubstWalker) eagerly emit and set
    // mirror_offset_. The bulk std::visit fallback below should be
    // unreachable; assert and return.
    LOGOS_ASSERT(p.mirror_offset_ != hermes::arena_offset_t{},
                 "B6.S35.S3",
                 "emit_pat: Pattern reached without mirror_offset_ set "
                 "(construction site missed direct-emit migration)");
    if (auto it = table_.pat.find(&p); it != table_.pat.end()) {
        if (it->second == p.mirror_offset_) return it->second;
        it->second = p.mirror_offset_;
        return p.mirror_offset_;
    }
    table_.pat[&p] = p.mirror_offset_;
    return p.mirror_offset_;
}


// ──────────────────────────────────────────────────────────────────────────
// LStmt mirror
// ──────────────────────────────────────────────────────────────────────────

hermes::arena_offset_t LirMirrorEmitter::emit_stmt(const LStmt& s) {
    // B.6 Stage 3.5 step 5: mirror_offset_ is field-as-truth. All LStmt
    // construction sites (sema make_stmt_emit, sema LirBuilder stmt_*,
    // mono subst_stmt) eagerly emit and set mirror_offset_ via per-kind
    // direct emitters; children (LExpr/LBlock) are registered transitively
    // by those direct emitters' internal expr_av/block_av calls. The bulk
    // std::visit fallback below is now unreachable.
    LOGOS_ASSERT(s.mirror_offset_ != hermes::arena_offset_t{},
                 "B6.S35.S5",
                 "emit_stmt: LStmt reached without mirror_offset_ set "
                 "(construction site missed direct-emit migration)");
    if (auto it = table_.stmt.find(&s); it != table_.stmt.end()) {
        if (it->second == s.mirror_offset_) return it->second;
        table_.stmt_by_offset.erase(it->second.value());
        it->second = s.mirror_offset_;
        table_.stmt_by_offset[s.mirror_offset_.value()] = &s;
        return s.mirror_offset_;
    }
    table_.stmt[&s] = s.mirror_offset_;
    table_.stmt_by_offset[s.mirror_offset_.value()] = &s;
    return s.mirror_offset_;
}


// ──────────────────────────────────────────────────────────────────────────
// LExpr mirror — biggest switch
// ──────────────────────────────────────────────────────────────────────────

hermes::arena_offset_t LirMirrorEmitter::emit_expr(const LExpr& e) {
    // B.6 Stage 3.5 step 7d: mirror_offset_ is field-as-truth. All LExpr
    // construction sites (sema LirBuilder direct(), mono subst_expr per-kind
    // direct emitters, closure_box) eagerly emit and set mirror_offset_ via
    // per-kind direct emitters; children are registered transitively. The
    // bulk std::visit body that previously walked LExpr::kind is gone.
    LOGOS_ASSERT(e.mirror_offset_ != hermes::arena_offset_t{},
                 "B6.S35.S7D",
                 "emit_expr: LExpr reached without mirror_offset_ set "
                 "(construction site missed direct-emit migration)");
    if (auto it = table_.expr.find(&e); it != table_.expr.end()) {
        if (it->second == e.mirror_offset_) return it->second;
        table_.expr_by_offset.erase(it->second.value());
        it->second = e.mirror_offset_;
        table_.expr_by_offset[e.mirror_offset_.value()] = &e;
        return e.mirror_offset_;
    }
    table_.expr[&e] = e.mirror_offset_;
    table_.expr_by_offset[e.mirror_offset_.value()] = &e;
    return e.mirror_offset_;
}

// ──────────────────────────────────────────────────────────────────────────
// Top-level driver
// ──────────────────────────────────────────────────────────────────────────

void LirMirrorEmitter::run(lir::LProgram& prog) {
    auto walk_fn = [&](LFunction& f) {
        // Skip extern (no body) and metaprog stubs (synthetic, never cloned).
        // from_binary_module functions DO have bodies and DO get cloned by
        // mono — their EPackExpand/etc. must be mirrored so subst_expr can
        // dispatch via lir_view.
        if (f.is_extern || f.is_metaprog_stub) return;
        emit_block(f.body);
    };
    for (auto& f : prog.functions)        walk_fn(*f);
    for (auto& f : prog.specializations)  walk_fn(*f);
    for (auto& s : prog.structs)
        for (auto& m : s.methods) walk_fn(*m);
    for (auto& s : prog.struct_specializations)
        for (auto& m : s.methods) walk_fn(*m);
    for (auto& i : prog.impls)
        for (auto& m : i.methods) walk_fn(*m);
    for (auto& t : prog.traits) {
        // Trait method signatures don't carry bodies in LIR yet — skip.
        (void)t;
    }
    for (auto& c : prog.consts)
        if (c.value) emit_expr(*c.value);
}

} // namespace

// Out-of-line LProgram special members — declared in lir.hpp, defined here
// where LirMirrorTable is complete (unique_ptr<LirMirrorTable> dtor needs it).
} // namespace logos::compiler

namespace logos::compiler::lir {
// Stage 3g.1 — mirror_table is non-null from construction so LirBuilder can
// emit per-node mirrors eagerly during sema, instead of waiting for a
// post-sema bulk pass.
LProgram::LProgram() : mirror_table(std::make_unique<::logos::compiler::LirMirrorTable>()) {}
LProgram::~LProgram() = default;
LProgram::LProgram(LProgram&&) noexcept = default;
LProgram& LProgram::operator=(LProgram&&) noexcept = default;
} // namespace logos::compiler::lir

namespace logos::compiler {

LirMirrorTable lir_mirror_emit(lir::LProgram& prog) {
    LirMirrorTable table;
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, table, prog.type_pool);
    em.run(prog);
    return table;
}

void lir_mirror_emit_function(lir::LProgram& prog,
                              LirMirrorTable& table,
                              lir::LFunction& fn) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, table, prog.type_pool);
    em.emit_function(fn);
}

void lir_mirror_emit_into(lir::LProgram& prog, LirMirrorTable& table) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, table, prog.type_pool);
    em.run(prog);
}

hermes::arena_offset_t lir_mirror_emit_lit_bool(lir::LProgram& prog, TypeRef ty, bool v) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_lit_bool_direct(ty, v);
}
hermes::arena_offset_t lir_mirror_emit_lit_int(lir::LProgram& prog, TypeRef ty, int64_t v) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_lit_int_direct(ty, v);
}
hermes::arena_offset_t lir_mirror_emit_lit_float(lir::LProgram& prog, TypeRef ty, double v) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_lit_float_direct(ty, v);
}
hermes::arena_offset_t lir_mirror_emit_lit_str(lir::LProgram& prog, TypeRef ty, std::string_view v) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_lit_str_direct(ty, v);
}
hermes::arena_offset_t lir_mirror_emit_var_ref(lir::LProgram& prog, TypeRef ty, std::string_view name) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_var_ref_direct(ty, name);
}
hermes::arena_offset_t lir_mirror_emit_addr_of(lir::LProgram& prog, TypeRef ty, std::string_view var_name) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_addr_of_direct(ty, var_name);
}
hermes::arena_offset_t lir_mirror_emit_pack_expand(lir::LProgram& prog, TypeRef ty, std::string_view var_name) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_pack_expand_direct(ty, var_name);
}
hermes::arena_offset_t lir_mirror_emit_size_of(lir::LProgram& prog, TypeRef ty, TypeRef elem) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_size_of_direct(ty, elem);
}
hermes::arena_offset_t lir_mirror_emit_align_of(lir::LProgram& prog, TypeRef ty, TypeRef elem) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_align_of_direct(ty, elem);
}
hermes::arena_offset_t lir_mirror_emit_generic_ref(lir::LProgram& prog, TypeRef ty,
                                                    std::string_view name,
                                                    const std::vector<TypeRef>& type_args) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_generic_ref_direct(ty, name, type_args);
}
hermes::arena_offset_t lir_mirror_emit_type_code_of(lir::LProgram& prog, TypeRef ty, TypeRef elem) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_type_code_of_direct(ty, elem);
}
hermes::arena_offset_t lir_mirror_emit_reflect_of(lir::LProgram& prog, TypeRef ty, TypeRef elem) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_reflect_of_direct(ty, elem);
}

hermes::arena_offset_t lir_mirror_emit_enum_lit(lir::LProgram& prog, TypeRef ty, std::string_view enum_name, std::string_view variant, int64_t disc) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_enum_lit_direct(ty, enum_name, variant, disc);
}
hermes::arena_offset_t lir_mirror_emit_enum_lit_data(lir::LProgram& prog, TypeRef ty, std::string_view enum_name, std::string_view variant, int64_t disc, const std::vector<lir::LExprPtr>& payload) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_enum_lit_data_direct(ty, enum_name, variant, disc, payload);
}
hermes::arena_offset_t lir_mirror_emit_struct_lit(lir::LProgram& prog, TypeRef ty, std::string_view name, const std::vector<std::pair<std::string, lir::LExprPtr>>& fields) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_struct_lit_direct(ty, name, fields);
}
hermes::arena_offset_t lir_mirror_emit_call(lir::LProgram& prog, TypeRef ty, std::string_view callee, const std::vector<TypeRef>& type_args, const std::vector<lir::LExprPtr>& args) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_call_direct(ty, callee, type_args, args);
}
hermes::arena_offset_t lir_mirror_emit_method_call(lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& receiver, std::string_view method, std::string_view resolved_symbol, const std::vector<TypeRef>& type_args, const std::vector<lir::LExprPtr>& args, int32_t vtable_index, std::string_view resolved_type, std::string_view tag_system, std::string_view tag_trait) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_method_call_direct(ty, receiver, method, resolved_symbol, type_args, args, vtable_index, resolved_type, tag_system, tag_trait);
}
hermes::arena_offset_t lir_mirror_emit_unary(lir::LProgram& prog, TypeRef ty, std::string_view op, const lir::LExprPtr& operand) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_unary_direct(ty, op, operand);
}
hermes::arena_offset_t lir_mirror_emit_bin_op(lir::LProgram& prog, TypeRef ty, std::string_view op, const lir::LExprPtr& lhs, const lir::LExprPtr& rhs) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_bin_op_direct(ty, op, lhs, rhs);
}
hermes::arena_offset_t lir_mirror_emit_field_read(lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& receiver, std::string_view field) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_field_read_direct(ty, receiver, field);
}
hermes::arena_offset_t lir_mirror_emit_index_read(lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& receiver, const lir::LExprPtr& index) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_index_read_direct(ty, receiver, index);
}
hermes::arena_offset_t lir_mirror_emit_hermes_lit(lir::LProgram& prog, TypeRef ty, const lir::HermesValPtr& root, bool has_captures, const std::vector<lir::LExprPtr>& capture_exprs, const std::vector<TypeRef>& capture_types, uint32_t capture_param_count, std::string_view static_blob) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_hermes_lit_direct(ty, root, has_captures, capture_exprs, capture_types, capture_param_count, static_blob);
}
hermes::arena_offset_t lir_mirror_emit_deref(lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& operand) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_deref_direct(ty, operand);
}
hermes::arena_offset_t lir_mirror_emit_cast(lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& operand, std::string_view hermes_build_fn) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_cast_direct(ty, operand, hermes_build_fn);
}
hermes::arena_offset_t lir_mirror_emit_try(lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& inner, int32_t ok_disc, int32_t err_disc) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_try_direct(ty, inner, ok_disc, err_disc);
}
hermes::arena_offset_t lir_mirror_emit_slice_lit(lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& base, const lir::LExprPtr& len) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_slice_lit_direct(ty, base, len);
}
hermes::arena_offset_t lir_mirror_emit_slice_index(lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& slice, const lir::LExprPtr& index) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_slice_index_direct(ty, slice, index);
}
hermes::arena_offset_t lir_mirror_emit_slice_len(lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& slice) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_slice_len_direct(ty, slice);
}
hermes::arena_offset_t lir_mirror_emit_slice_ptr(lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& slice) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_slice_ptr_direct(ty, slice);
}
hermes::arena_offset_t lir_mirror_emit_addr_of_temp(lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& inner, bool is_mut) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_addr_of_temp_direct(ty, inner, is_mut);
}
hermes::arena_offset_t lir_mirror_emit_ptr_arith(lir::LProgram& prog, TypeRef ty, uint8_t op, const lir::LExprPtr& ptr, const lir::LExprPtr& offset) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_ptr_arith_direct(ty, op, ptr, offset);
}
hermes::arena_offset_t lir_mirror_emit_ptr_diff(lir::LProgram& prog, TypeRef ty, bool by_byte, const lir::LExprPtr& lhs, const lir::LExprPtr& rhs) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_ptr_diff_direct(ty, by_byte, lhs, rhs);
}
hermes::arena_offset_t lir_mirror_emit_if_expr(lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& cond, const lir::LExprPtr& then_val, const lir::LExprPtr& else_val) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_if_expr_direct(ty, cond, then_val, else_val);
}
hermes::arena_offset_t lir_mirror_emit_tuple_lit(lir::LProgram& prog, TypeRef ty, const std::vector<lir::LExprPtr>& elems) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_tuple_lit_direct(ty, elems);
}
hermes::arena_offset_t lir_mirror_emit_tuple_index(lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& receiver, uint32_t index) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_tuple_index_direct(ty, receiver, index);
}
hermes::arena_offset_t lir_mirror_emit_arr_lit(lir::LProgram& prog, TypeRef ty, const std::vector<lir::LExprPtr>& elems) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_arr_lit_direct(ty, elems);
}
hermes::arena_offset_t lir_mirror_emit_block_expr(lir::LProgram& prog, TypeRef ty, const lir::LBlock* block, const lir::LExprPtr& result) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_block_expr_direct(ty, block, result);
}
hermes::arena_offset_t lir_mirror_emit_closure_call(lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& callee, const std::vector<lir::LExprPtr>& args) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_closure_call_direct(ty, callee, args);
}
hermes::arena_offset_t lir_mirror_emit_fn_ptr_call(lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& callee, const std::vector<lir::LExprPtr>& args) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_fn_ptr_call_direct(ty, callee, args);
}
hermes::arena_offset_t lir_mirror_emit_match_expr(lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& scrut, const std::vector<lir::EMatchArm>& arms) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_match_expr_direct(ty, scrut, arms);
}
hermes::arena_offset_t lir_mirror_emit_format_call(lir::LProgram& prog, TypeRef ty, const lir::LExprPtr& fmt, const std::vector<lir::LExprPtr>& args, const std::vector<TypeRef>& arg_types) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_format_call_direct(ty, fmt, args, arg_types);
}
hermes::arena_offset_t lir_mirror_emit_closure_box(lir::LProgram& prog, TypeRef ty, const lir::EClosure* inner) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_closure_box_direct(ty, inner);
}

// ── Stage B.6 — LStmt direct mirror writers ──────────────────────────────
hermes::arena_offset_t lir_mirror_emit_let(lir::LProgram& prog, uint32_t line, std::string_view name, TypeRef ty, const lir::LExprPtr& value, bool is_mut) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_let_direct(line, name, ty, value, is_mut);
}
hermes::arena_offset_t lir_mirror_emit_assign(lir::LProgram& prog, uint32_t line, std::string_view name, const lir::LExprPtr& value) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_assign_direct(line, name, value);
}
hermes::arena_offset_t lir_mirror_emit_return(lir::LProgram& prog, uint32_t line, const lir::LExprPtr& value) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_return_direct(line, value);
}
hermes::arena_offset_t lir_mirror_emit_if_stmt(lir::LProgram& prog, uint32_t line, const lir::LExprPtr& cond, const lir::LBlock* then_blk, const lir::LBlock* else_blk) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_if_stmt_direct(line, cond, then_blk, else_blk);
}
hermes::arena_offset_t lir_mirror_emit_while(lir::LProgram& prog, uint32_t line, const lir::LExprPtr& cond, const lir::LBlock* body, std::string_view label) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_while_direct(line, cond, body, label);
}
hermes::arena_offset_t lir_mirror_emit_for(lir::LProgram& prog, uint32_t line, std::string_view var, const lir::LExprPtr& lo, const lir::LExprPtr& hi, bool inclusive, const lir::LBlock* body, std::string_view label) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_for_direct(line, var, lo, hi, inclusive, body, label);
}
hermes::arena_offset_t lir_mirror_emit_loop(lir::LProgram& prog, uint32_t line, const lir::LBlock* body, std::string_view label, std::string_view break_slot, TypeRef result_type) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_loop_direct(line, body, label, break_slot, result_type);
}
hermes::arena_offset_t lir_mirror_emit_break(lir::LProgram& prog, uint32_t line, const lir::LExprPtr& value, std::string_view label) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_break_direct(line, value, label);
}
hermes::arena_offset_t lir_mirror_emit_continue(lir::LProgram& prog, uint32_t line, std::string_view label) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_continue_direct(line, label);
}
hermes::arena_offset_t lir_mirror_emit_block_stmt(lir::LProgram& prog, uint32_t line, const lir::LBlock* body) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_block_stmt_direct(line, body);
}
hermes::arena_offset_t lir_mirror_emit_field_write(lir::LProgram& prog, uint32_t line, std::string_view receiver, std::string_view field, const lir::LExprPtr& value) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_field_write_direct(line, receiver, field, value);
}
hermes::arena_offset_t lir_mirror_emit_index_write(lir::LProgram& prog, uint32_t line, std::string_view arr, const lir::LExprPtr& index, const lir::LExprPtr& value) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_index_write_direct(line, arr, index, value);
}
hermes::arena_offset_t lir_mirror_emit_field_index_write(lir::LProgram& prog, uint32_t line, std::string_view receiver, std::string_view field, const lir::LExprPtr& index, const lir::LExprPtr& value) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_field_index_write_direct(line, receiver, field, index, value);
}
hermes::arena_offset_t lir_mirror_emit_expr_stmt(lir::LProgram& prog, uint32_t line, const lir::LExprPtr& expr) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_expr_stmt_direct(line, expr);
}
hermes::arena_offset_t lir_mirror_emit_match_stmt(lir::LProgram& prog, uint32_t line, const lir::LExprPtr& scrut, const std::vector<lir::LMatchArm>& arms) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_match_stmt_direct(line, scrut, arms);
}
hermes::arena_offset_t lir_mirror_emit_for_each(lir::LProgram& prog, uint32_t line, std::string_view var, const lir::LExprPtr& iter, TypeRef elem_type, int64_t arr_size, bool is_slice, const lir::LBlock* body) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_for_each_direct(line, var, iter, elem_type, arr_size, is_slice, body);
}
hermes::arena_offset_t lir_mirror_emit_deref_write(lir::LProgram& prog, uint32_t line, const lir::LExprPtr& ptr, const lir::LExprPtr& value) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_deref_write_direct(line, ptr, value);
}
hermes::arena_offset_t lir_mirror_emit_drop(lir::LProgram& prog, uint32_t line, std::string_view var_name, std::string_view drop_fn, TypeRef ty, bool drop_fields, const std::vector<std::string>& moved_fields) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_drop_direct(line, var_name, drop_fn, ty, drop_fields, moved_fields);
}
hermes::arena_offset_t lir_mirror_emit_deref_field_write(lir::LProgram& prog, uint32_t line, std::string_view receiver, std::string_view type_name, std::string_view field, const lir::LExprPtr& value) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_deref_field_write_direct(line, receiver, type_name, field, value);
}
hermes::arena_offset_t lir_mirror_emit_tuple_write(lir::LProgram& prog, uint32_t line, std::string_view receiver, uint32_t index, const lir::LExprPtr& value, TypeRef recv_type) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_tuple_write_direct(line, receiver, index, value, recv_type);
}
hermes::arena_offset_t lir_mirror_emit_let_else(lir::LProgram& prog, uint32_t line, const lir::Pattern& pat, const lir::LExprPtr& scrut, const lir::LBlock* else_block, const std::vector<lir::LExprPtr>& guards) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_let_else_direct(line, pat, scrut, else_block, guards);
}
hermes::arena_offset_t lir_mirror_emit_chain_field_write(lir::LProgram& prog, uint32_t line, std::string_view receiver, std::string_view mid_field, const std::vector<std::string>& extras, std::string_view field, const lir::LExprPtr& value) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_chain_field_write_direct(line, receiver, mid_field, extras, field, value);
}

// ── Stage B.6 — HermesVal direct mirror writers ──────────────────────────
hermes::arena_offset_t lir_mirror_emit_hv_null(lir::LProgram& prog) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_hv_null_direct();
}
hermes::arena_offset_t lir_mirror_emit_hv_bool(lir::LProgram& prog, bool value) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_hv_bool_direct(value);
}
hermes::arena_offset_t lir_mirror_emit_hv_int(lir::LProgram& prog, int64_t value) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_hv_int_direct(value);
}
hermes::arena_offset_t lir_mirror_emit_hv_float(lir::LProgram& prog, double value) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_hv_float_direct(value);
}
hermes::arena_offset_t lir_mirror_emit_hv_str(lir::LProgram& prog, std::string_view value) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_hv_str_direct(value);
}
hermes::arena_offset_t lir_mirror_emit_hv_map(lir::LProgram& prog, const std::vector<lir::HVMapEntry>& entries, std::string_view key_type) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_hv_map_direct(entries, key_type);
}
hermes::arena_offset_t lir_mirror_emit_hv_array(lir::LProgram& prog, const std::vector<lir::HermesValPtr>& elements, std::string_view elem_type) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_hv_array_direct(elements, elem_type);
}
hermes::arena_offset_t lir_mirror_emit_hv_capture(lir::LProgram& prog, uint32_t param_index, uint32_t value_index) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_hv_capture_direct(param_index, value_index);
}
hermes::arena_offset_t lir_mirror_emit_hv_type(lir::LProgram& prog, uint32_t kind, uint64_t uid, std::string_view name) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_hv_type_direct(kind, uid, name);
}

// ── Stage B.6 — Pattern direct mirror writers ────────────────────────────
hermes::arena_offset_t lir_mirror_emit_pat_variant(lir::LProgram& prog, std::string_view enum_name, std::string_view variant, int64_t disc) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_variant_direct(enum_name, variant, disc);
}
hermes::arena_offset_t lir_mirror_emit_pat_int(lir::LProgram& prog, int64_t value) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_int_direct(value);
}
hermes::arena_offset_t lir_mirror_emit_pat_bool(lir::LProgram& prog, bool value) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_bool_direct(value);
}
hermes::arena_offset_t lir_mirror_emit_pat_wild(lir::LProgram& prog, std::string_view name) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_wild_direct(name);
}
hermes::arena_offset_t lir_mirror_emit_pat_variant_data(lir::LProgram& prog, std::string_view enum_name, std::string_view variant, int64_t disc, const std::vector<std::string>& bindings, const std::vector<TypeRef>& binding_types) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_variant_data_direct(enum_name, variant, disc, bindings, binding_types);
}
hermes::arena_offset_t lir_mirror_emit_pat_or(lir::LProgram& prog, const std::vector<lir::Pattern>& alts) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_or_direct(alts);
}
hermes::arena_offset_t lir_mirror_emit_pat_tuple(lir::LProgram& prog, const std::vector<std::string>& bindings, const std::vector<TypeRef>& binding_types, const std::vector<lir::Pattern>& subs) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_tuple_direct(bindings, binding_types, subs);
}
hermes::arena_offset_t lir_mirror_emit_pat_range(lir::LProgram& prog, int64_t lo, int64_t hi) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_range_direct(lo, hi);
}
hermes::arena_offset_t lir_mirror_emit_pat_struct(lir::LProgram& prog, std::string_view struct_name, const std::vector<lir::PatFieldBinding>& fields, bool has_rest) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_struct_direct(struct_name, fields, has_rest);
}
hermes::arena_offset_t lir_mirror_emit_pat_slice(lir::LProgram& prog, const std::vector<lir::Pattern>& prefix, const std::vector<lir::Pattern>& rest, const std::vector<lir::Pattern>& suffix) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_slice_direct(prefix, rest, suffix);
}
hermes::arena_offset_t lir_mirror_emit_pat_at(lir::LProgram& prog, std::string_view name, const std::vector<lir::Pattern>& sub, TypeRef type) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_at_direct(name, sub, type);
}
hermes::arena_offset_t lir_mirror_emit_pat_ref_bind(lir::LProgram& prog, std::string_view name, bool is_mut, TypeRef bind_type) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_ref_bind_direct(name, is_mut, bind_type);
}
hermes::arena_offset_t lir_mirror_emit_pat_ref_pat(lir::LProgram& prog, const std::vector<lir::Pattern>& inner, bool is_mut) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_ref_pat_direct(inner, is_mut);
}

void lir_mirror_retype_expr(lir::LProgram& prog,
                            hermes::arena_offset_t expr_off,
                            TypeRef new_ty) {
    if (expr_off == hermes::arena_offset_t{}) return;
    auto& arena = prog.type_pool.arena_or_init();
    auto* tom = reinterpret_cast<hermes::TinyObjectMap*>(
        arena.head().data() + expr_off.value());
    auto av = new_ty ? hermes::AnyVal::from_offset(new_ty.offset())
                     : hermes::AnyVal{};
    if (av.is_null()) return;
    auto r = tom->put(lir_schema::expr_common::TYPE.code, av, arena);
    LOGOS_ASSERT(r.has_value(), "LIR-MIRROR-006",
        "retype_expr put failed");
}

void lir_mirror_populate_moved(lir::LProgram& prog, LirMirrorTable& table) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, table, prog.type_pool);
    for (auto& i : prog.impls)
        for (auto& m : i.methods) em.emit_function(*m);
    for (auto& c : prog.consts)
        if (c.value) em.emit_expr_public(*c.value);
    // Stage 2 (Group 1+) fix: direct-emitted nodes leave kind=ELitInt default,
    // so emit_expr's back-fill walk-via-variant misses their descendants. Sweep
    // the pools to register every LExpr/LStmt/LBlock that already carries a
    // mirror_offset_ but whose descendants weren't reached by the recursive
    // visit. Cheap (one-time, post-mono) and unconditional — works for both
    // variant-built and direct-built nodes.
    // M5 step 4: expr_pool_ is now shared_ptr<vector<...>>; deref to iterate.
    if (prog.expr_pool_) {
        for (auto& uptr : *prog.expr_pool_)
            if (uptr && uptr->mirror_offset_ != hermes::arena_offset_t{})
                table.expr_by_offset[uptr->mirror_offset_.value()] = uptr.get();
    }
}

// ── Per-node entry points (Stage 3g.1) ────────────────────────────────────
//
// LirBuilder calls these immediately after constructing each variant. The
// emitter's per-node emit_* functions are memoized via the table, so a node
// emitted here is a cache hit when later walked by lir_mirror_emit_into /
// lir_mirror_emit_function — which keeps existing post-sema and per-clone
// passes correct without modification.

hermes::arena_offset_t lir_mirror_emit_expr_node(lir::LProgram& prog, const lir::LExpr& e) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_expr_public(e);
}
hermes::arena_offset_t lir_mirror_emit_stmt_node(lir::LProgram& prog, const lir::LStmt& s) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_stmt_public(s);
}
hermes::arena_offset_t lir_mirror_emit_block_node(lir::LProgram& prog, const lir::LBlock& b) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_block_public(b);
}
hermes::arena_offset_t lir_mirror_emit_pat_node(lir::LProgram& prog, const lir::Pattern& p) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_public(p);
}
hermes::arena_offset_t lir_mirror_emit_hv_node(lir::LProgram& prog, const lir::HermesVal& v) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table, prog.type_pool);
    return em.emit_hv_public(v);
}

void lir_mirror_update_type(lir::LProgram& prog, const lir::LExpr& e) {
    // Phase 5.B step 2 prerequisite: sema modifies LExpr.type AFTER initial
    // mirror emit at 5 sites (sema_stmt:1244, sema_expr:629/1419/4408/12216).
    // Overwrite the mirror's TYPE field in-place so view-based readers
    // (mlir_gen, borrow_check, mono_scan, region_infer, mono cross-arena
    // subst_expr) observe the post-construction type.
    //
    // TinyObjectMap::put for an existing key is in-place (no arena grow),
    // so this is cheap and safe to call repeatedly.
    if (e.mirror_offset_ == hermes::arena_offset_t{}) return;
    auto& arena = prog.type_pool.arena_or_init();
    auto* base = arena.head().data();
    auto* tom = reinterpret_cast<hermes::TinyObjectMap*>(
                    base + e.mirror_offset_.value());
    auto av = e.type
              ? hermes::AnyVal::from_offset(e.type.offset())
              : hermes::AnyVal{};
    // Ignore put() error: TYPE key was already present when the mirror
    // was first emitted (every emit_*_direct that gets a non-null ty
    // writes the TYPE key), so this is an in-place overwrite that
    // cannot OOM. If TYPE was absent (e.g. construction-time ty was
    // null), put() may need to grow the TinyObjectMap; in that case
    // it returns error but we have nothing useful to do — the original
    // mirror just stays with no TYPE key, same as it was before this
    // helper was called. Either way, observable state is consistent.
    (void) tom->put(lir_schema::expr_common::TYPE.code, av, arena);
}

} // namespace logos::compiler
