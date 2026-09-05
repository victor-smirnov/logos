// Logos project — https://github.com/victor-smirnov/logos
//
// Phase 3b — Writ mirror emitter for L-IR.
//
// Walks every function/const initializer in an LProgram and writes a
// TinyObjectMap mirror per node into the program's TypePool arena. Mirrors
// are not consumed yet (Phase 3d does that); for now the emitter validates
// that every L-IR variant has a faithful Writ representation.

#include <logos/compiler/lir_mirror.hpp>
#include <logos/compiler/lir_schema.hpp>
#include <logos/compiler/lir_view.hpp>  // header-compile smoke until 3d uses it
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/verification/assert.hpp>

#include <variant>

namespace logos::compiler {

using lir::LMatchArm;
using lir::Pattern;
using lir::WritVal;
using lir::EClosure;

namespace {

namespace ek = lir_schema::expr_keys;
namespace ec = lir_schema::expr_common;
namespace sk = lir_schema::stmt_keys;
namespace sc = lir_schema::stmt_common;
namespace dk = lir_schema::decl_keys;
namespace pmk = lir_schema::param_keys;
namespace ftpk = lir_schema::fn_tparam_keys;
namespace tbk = lir_schema::fn_tbound_keys;
namespace wbk = lir_schema::fn_wherebound_keys;
namespace fldk = lir_schema::field_keys;
namespace ank  = lir_schema::annot_keys;
namespace akvk = lir_schema::annkv_keys;
namespace avk  = lir_schema::annval_keys;
namespace pk = lir_schema::pat_keys;
namespace ak = lir_schema::arm_keys;
namespace hl = lir_schema::writ_lit_keys;
namespace hk = lir_schema::hv_keys;
namespace ck = lir_schema::closure_keys;
namespace pdk = lir_schema::ptrdiff_keys;

class LirMirrorEmitter {
    // THE document handle: all object creation goes through ctr_ (make_string /
    // make_array / make_tiny_map); raw arena access is encapsulated in views.
    writ::WritCtr& ctr_;
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
    LirMirrorEmitter(writ::WritCtr& c, LirMirrorTable& t)
        : ctr_(c), table_(t) {}
    LirMirrorEmitter(writ::WritCtr& c, LirMirrorTable& t, TypePool& p)
        : ctr_(c), table_(t), pool_(&p) {}

    writ::MemHolder* holder() const noexcept { return ctr_.holder(); }

    // Reference a child by its absolute mirror ADDRESS (self-relative, no base).
    // Every emitter object handle (make_map/make_array/emit_* result) is now an
    // address — segments never move, so the pointer is a stable child reference.
    writ::AnyVal mref_addr(const uint8_t* p) const noexcept {
        writ::AnyVal a; if (p) a.set_ref(p); return a;
    }

    void run(lir::LProgram& prog);

    // Public per-node entry points (Stage 3g.1). Called from
    // lir_mirror_emit_*_node free functions; idempotent via table cache.
    const uint8_t* emit_block_stmts_public(const std::vector<lir_view::StmtRef>& s) { return emit_block_stmts(s); }
    const uint8_t* emit_pat_public  (const Pattern& p)  { return emit_pat(p); }
    const uint8_t* emit_hv_public   (const WritVal& v){ return emit_hv(v); }

    // Stage 2 — variant-free direct mirror writers. Allocate a fresh map for
    // a single expr kind without reading from a variant payload. Used by
    // LirBuilder / mono_clone after Stage 2 retires the variant alternative
    // for that kind.
    const uint8_t* emit_lit_bool_direct(TypeRef ty, bool v) {
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::LitBool));
        put(map_off, ek::LIT_BOOL, put_bool(v));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_lit_int_direct(TypeRef ty, int64_t v) {
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::LitInt));
        put(map_off, ek::LIT_I64, put_i64(v));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    // 128-bit variant: low half in LIT_I64, high half in LIT_I64_HI (omitted
    // when 0, so 64-bit-fitting values stay byte-identical to the i64 path).
    const uint8_t* emit_lit_int_direct_128(TypeRef ty, uint64_t lo, uint64_t hi) {
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::LitInt));
        put(map_off, ek::LIT_I64, put_i64((int64_t)lo));
        if (hi != 0) put(map_off, ek::LIT_I64_HI, put_i64((int64_t)hi));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_lit_float_direct(TypeRef ty, double v) {
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::LitFloat));
        put(map_off, ek::LIT_F64, put_f64(v));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_lit_str_direct(TypeRef ty, std::string_view v) {
        auto s_av = put_string(v);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::LitStr));
        put(map_off, ek::LIT_STR, s_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_var_ref_direct(TypeRef ty, std::string_view name,
                                               uint32_t slot = 0xFFFFFFFFu) {
        auto n_av = put_string(name);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::VarRef));
        put(map_off, ek::NAME, n_av);
        if (slot != 0xFFFFFFFFu) put(map_off, ek::VAR_SLOT, put_i64((int64_t)slot));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_addr_of_direct(TypeRef ty, std::string_view var_name) {
        auto n_av = put_string(var_name);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::AddrOf));
        put(map_off, ek::NAME, n_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }

    // Stage E: declaration-layer mirror writers.
    const uint8_t* emit_type_alias_direct(std::string_view name, TypeRef type,
                                          std::string_view doc) {
        auto n_av = put_string(name);
        auto d_av = doc.empty() ? writ::AnyVal{} : put_string(doc);
        auto map_off = make_map(writ::schema::lir_stmt(
            int32_t(lir_schema::decl::Code::TypeAlias)));
        put(map_off, dk::NAME, n_av);
        if (type) put(map_off, dk::TYPE_REF, type_av(type));
        if (!d_av.is_null()) put(map_off, dk::DOC, d_av);
        return map_off;
    }
    // ── Function decl sub-map builders (Stage E FunctionDraft migration) ──────
    // PARAMS array element (LParam sub-map). Own key space.
    writ::AnyVal param_av(const lir::LParam& p) {
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Count + 5));
        put(map_off, pmk::P_NAME, put_string(p.name));
        if (p.type)           put(map_off, pmk::P_TYPE,           type_av(p.type));
        if (p.is_variadic)    put(map_off, pmk::P_IS_VARIADIC,    put_bool(true));
        if (p.owning_box_dyn) put(map_off, pmk::P_OWNING_BOX_DYN, put_bool(true));
        if (p.slot != 0xFFFFFFFFu) put(map_off, pmk::P_SLOT, put_i64((int64_t)p.slot));
        return mref_addr(map_off);
    }
    // FTP_BOUNDS array element (TraitBound sub-map). Own key space.
    writ::AnyVal tbound_av(const TraitBound& tb) {
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Count + 7));
        put(map_off, tbk::TB_TRAIT_NAME, put_string(tb.trait_name));
        // The bound's always-qualified trait identity, so a bound that crosses
        // an archive boundary still denotes the trait it was WRITTEN against.
        // Sparse: written only when it differs from the spelling.
        if (!tb.identity_trait.empty() && tb.identity_trait != tb.trait_name)
            put(map_off, tbk::TB_IDENTITY, put_string(tb.identity_trait));
        auto a = type_array(tb.type_args);
        if (!a.is_null()) put(map_off, tbk::TB_TYPE_ARGS, a);
        if (!tb.hrtb_binders.empty()) {
            auto arr_off = make_array(tb.hrtb_binders.size());
            for (auto& s : tb.hrtb_binders) array_push(arr_off, put_string(s));
            put(map_off, tbk::TB_HRTB_BINDERS, mref_addr(arr_off));
        }
        return mref_addr(map_off);
    }
    // TYPE_PARAMS / IMPL_TYPE_PARAMS array element (fn TypeParam sub-map; richer
    // than enum's — carries bounds/const/default). Own key space.
    writ::AnyVal fn_tparam_av(const TypeParam& tp) {
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Count + 6));
        put(map_off, ftpk::FTP_NAME, put_string(tp.name));
        if (tp.is_variadic)  put(map_off, ftpk::FTP_IS_VARIADIC, put_bool(true));
        if (tp.is_const)     put(map_off, ftpk::FTP_IS_CONST,    put_bool(true));
        if (tp.const_type)   put(map_off, ftpk::FTP_CONST_TYPE,  type_av(tp.const_type));
        if (tp.default_type) put(map_off, ftpk::FTP_DEFAULT_TYPE,type_av(tp.default_type));
        if (!tp.bounds.empty()) {
            std::vector<writ::AnyVal> elems; elems.reserve(tp.bounds.size());
            for (auto& tb : tp.bounds) elems.push_back(tbound_av(tb));
            auto arr_off = make_array(elems.size());
            for (auto av : elems) array_push(arr_off, av);
            put(map_off, ftpk::FTP_BOUNDS, mref_addr(arr_off));
        }
        if (!tp.lifetime_outlives.empty()) {
            auto arr_off = make_array(tp.lifetime_outlives.size());
            for (auto& s : tp.lifetime_outlives) array_push(arr_off, put_string(s));
            put(map_off, ftpk::FTP_LIFETIME_OUTLIVES, mref_addr(arr_off));
        }
        return mref_addr(map_off);
    }
    // WHERE_TYPE_BOUNDS array element (subject type + trait name). Own key space.
    writ::AnyVal wherebound_av(const std::pair<TypeRef, std::string>& wb) {
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Count + 8));
        if (wb.first) put(map_off, wbk::WB_TYPE, type_av(wb.first));
        put(map_off, wbk::WB_TRAIT, put_string(wb.second));
        return mref_addr(map_off);
    }

    // ── StructDraft decl sub-map builders (Stage E struct migration) ───────────
    // FIELDS array element (LField sub-map; schema code stmt::Count+9). Own space.
    writ::AnyVal field_av(const lir::LField& fld) {
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Count + 9));
        put(map_off, fldk::F_NAME, put_string(fld.name));
        if (fld.type)        put(map_off, fldk::F_TYPE,        type_av(fld.type));
        if (fld.is_variadic) put(map_off, fldk::F_IS_VARIADIC, put_bool(true));
        if (!fld.doc.empty())put(map_off, fldk::F_DOC,         put_string(fld.doc));
        return mref_addr(map_off);
    }
    // ANNOTATIONS value sub-map (LAnnotationValue; stmt::Count+12). RECURSIVE.
    // Reader keys off AV_KIND; value fields emitted only when non-default.
    writ::AnyVal annval_av(const lir::LAnnotationValue& v) {
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Count + 12));
        put(map_off, avk::AV_KIND, put_i64((int64_t)v.kind));
        if (v.i != 0)               put(map_off, avk::AV_I,            put_i64(v.i));
        if (v.f != 0.0)             put(map_off, avk::AV_F,            put_f64(v.f));
        if (!v.s.empty())           put(map_off, avk::AV_S,            put_string(v.s));
        if (!v.enum_name.empty())   put(map_off, avk::AV_ENUM_NAME,    put_string(v.enum_name));
        if (!v.enum_variant.empty())put(map_off, avk::AV_ENUM_VARIANT, put_string(v.enum_variant));
        if (!v.arr.empty()) {
            std::vector<writ::AnyVal> elems; elems.reserve(v.arr.size());
            for (auto& child : v.arr) elems.push_back(annval_av(child));
            auto arr_off = make_array(elems.size());
            for (auto av : elems) array_push(arr_off, av);
            put(map_off, avk::AV_ARR, mref_addr(arr_off));
        }
        return mref_addr(map_off);
    }
    // A_KV array element (annotation kv-pair sub-map; stmt::Count+11). Own space.
    writ::AnyVal annkv_av(const std::pair<std::string, lir::LAnnotationValue>& kv) {
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Count + 11));
        put(map_off, akvk::KV_NAME,  put_string(kv.first));
        put(map_off, akvk::KV_VALUE, annval_av(kv.second));
        return mref_addr(map_off);
    }
    // ANNOTATIONS array element (LAnnotationInstance sub-map; stmt::Count+10).
    writ::AnyVal annot_av(const lir::LAnnotationInstance& ai) {
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Count + 10));
        put(map_off, ank::A_NAME, put_string(ai.ann_name));
        if (!ai.ann_pkg.empty()) put(map_off, ank::A_PKG, put_string(ai.ann_pkg));
        if (!ai.ann_fqn.empty()) put(map_off, ank::A_FQN, put_string(ai.ann_fqn));
        if (!ai.kv.empty()) {
            std::vector<writ::AnyVal> elems; elems.reserve(ai.kv.size());
            for (auto& kv : ai.kv) elems.push_back(annkv_av(kv));
            auto arr_off = make_array(elems.size());
            for (auto av : elems) array_push(arr_off, av);
            put(map_off, ank::A_KV, mref_addr(arr_off));
        }
        return mref_addr(map_off);
    }
    // Append a method (its already-emitted func decl map) to a stored struct's
    // mutable METHODS array IN PLACE. Sound because the ObjectArray header is
    // stable (grow re-points only the internal buffer — see object_array.hpp).
    // Used by the sema impl-block/re-attachment passes and mono's lazy
    // drain_method_worklist, which collect methods AFTER the struct is stored.
    void struct_append_method_direct(lir_view::StructView sv, lir_view::FunctionView m) {
        auto* map = const_cast<writ::TinyObjectMap*>(sv.self.mirror());
        auto cur = map->get(lir_schema::struct_keys::METHODS.code);
        LOGOS_ASSERT(!cur.is_null(), "LIR-MIRROR-STRUCT-METHODS",
                     "struct mirror has no METHODS array (lower_struct_def / "
                     "lower_spec_struct / clone_struct_def always create it)");
        auto* arr_addr = reinterpret_cast<const uint8_t*>(cur.as_ptr<const writ::ObjectArray>());
        array_push(arr_addr, mref_addr(m.self.addr()));
    }
    // Replace a stored struct's METHODS array with a fresh one holding exactly
    // `ms` (used by the SemaCache reset's filter_methods, which keeps only
    // binary-origin methods). The old array becomes garbage — rare path.
    void struct_set_methods_direct(lir_view::StructView sv,
                                   const std::vector<lir_view::FunctionView>& ms) {
        std::vector<writ::AnyVal> elems; elems.reserve(ms.size());
        for (auto m : ms) elems.push_back(mref_addr(m.self.addr()));
        auto arr_off = make_array(elems.size());
        for (auto av : elems) array_push(arr_off, av);
        put(sv.self.addr(), lir_schema::struct_keys::METHODS, mref_addr(arr_off));
    }
    // Set/overwrite a stored struct's TYPE_CODE scalar in place (sema applies a
    // trait-declared / explicit type_code AFTER the struct is stored). The
    // struct mirror's map has cap=40 with room for the key even when it was
    // absent at build time (the direct-build only writes TYPE_CODE when
    // non-zero).
    void struct_set_type_code_direct(lir_view::StructView sv, uint64_t code) {
        put(sv.self.addr(), lir_schema::struct_keys::TYPE_CODE,
            put_i64((int64_t)code));
    }
    const uint8_t* emit_pack_expand_direct(TypeRef ty, std::string_view var_name) {
        auto n_av = put_string(var_name);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::PackExpand));
        put(map_off, ek::NAME, n_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_size_of_direct(TypeRef ty, TypeRef elem) {
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::SizeOf));
        put(map_off, ek::ELEM_TYPE, type_av(elem));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_align_of_direct(TypeRef ty, TypeRef elem) {
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::AlignOf));
        put(map_off, ek::ELEM_TYPE, type_av(elem));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_generic_ref_direct(TypeRef ty,
                                                    std::string_view name,
                                                    const std::vector<TypeRef>& type_args) {
        auto cn_av = put_string(name);
        auto ta_av = type_array(type_args);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::GenericRef));
        put(map_off, ek::CALLEE,    cn_av);
        put(map_off, ek::TYPE_ARGS, ta_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_type_code_of_direct(TypeRef ty, TypeRef elem) {
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::TypeCodeOf));
        put(map_off, ek::ELEM_TYPE, type_av(elem));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_reflect_of_direct(TypeRef ty, TypeRef elem) {
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::ReflectOf));
        put(map_off, ek::ELEM_TYPE, type_av(elem));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }

    // Stage 2 Group 1 — children-only direct writers. Children must already
    // have their own mirror_ptr_ set (cache-hit fast path inside expr_av).
    const uint8_t* emit_deref_direct(TypeRef ty, lir_view::ExprRef operand) {
        auto o_av = expr_av(operand);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::Deref));
        put(map_off, ek::OPERAND, o_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_cast_direct(TypeRef ty, lir_view::ExprRef operand,
                                             std::string_view writ_build_fn) {
        auto o_av = expr_av(operand);
        writ::AnyVal hbf_av;
        bool has_hbf = !writ_build_fn.empty();
        if (has_hbf) hbf_av = put_string(writ_build_fn);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::Cast));
        put(map_off, ek::OPERAND, o_av);
        if (has_hbf) put(map_off, ek::WRIT_BUILD_FN, hbf_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_try_direct(TypeRef ty, lir_view::ExprRef inner,
                                            int32_t ok_disc, int32_t err_disc) {
        auto in_av = expr_av(inner);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::Try));
        put(map_off, ek::INNER,    in_av);
        put(map_off, ek::OK_DISC,  put_i32(ok_disc));
        put(map_off, ek::ERR_DISC, put_i32(err_disc));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_slice_lit_direct(TypeRef ty, lir_view::ExprRef base,
                                                  lir_view::ExprRef len) {
        auto b_av = expr_av(base);
        auto l_av = expr_av(len);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::SliceLit));
        put(map_off, ek::BASE_PTR, b_av);
        put(map_off, ek::LEN,      l_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_slice_index_direct(TypeRef ty, lir_view::ExprRef slice,
                                                    lir_view::ExprRef index) {
        auto s_av = expr_av(slice);
        auto i_av = expr_av(index);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::SliceIndex));
        put(map_off, ek::SLICE, s_av);
        put(map_off, ek::INDEX, i_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_slice_len_direct(TypeRef ty, lir_view::ExprRef slice) {
        auto s_av = expr_av(slice);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::SliceLen));
        put(map_off, ek::SLICE, s_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_slice_ptr_direct(TypeRef ty, lir_view::ExprRef slice) {
        auto s_av = expr_av(slice);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::SlicePtr));
        put(map_off, ek::SLICE, s_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_addr_of_temp_direct(TypeRef ty, lir_view::ExprRef inner,
                                                     bool is_mut) {
        auto in_av = expr_av(inner);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::AddrOfTemp));
        put(map_off, ek::INNER,  in_av);
        put(map_off, ek::IS_MUT, put_bool(is_mut));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    template <class ExprVec>
    const uint8_t* emit_writ_lit_direct(TypeRef ty,
                                                   const lir::WritValPtr& root,
                                                   bool has_captures,
                                                   const ExprVec& capture_exprs,
                                                   const std::vector<TypeRef>& capture_types,
                                                   uint32_t capture_param_count,
                                                   std::string_view static_blob) {
        auto root_av = hv_av(root);
        auto cap_ex  = expr_array(capture_exprs);
        auto cap_ty  = type_array(capture_types);
        auto blob_av = static_blob.empty()
                       ? writ::AnyVal{}
                       : put_string(static_blob);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::WritLit));
        put(map_off, hl::ROOT,                  root_av);
        put(map_off, hl::HAS_CAPTURES,          put_bool(has_captures));
        put(map_off, hl::CAPTURE_EXPRS,         cap_ex);
        put(map_off, hl::CAPTURE_TYPES,         cap_ty);
        put(map_off, hl::CAPTURE_PARAM_COUNT,   put_u32(capture_param_count));
        if (!static_blob.empty()) put(map_off, hl::STATIC_BLOB, blob_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_enum_lit_direct(TypeRef ty,
                                                 std::string_view enum_name,
                                                 std::string_view variant,
                                                 int64_t disc) {
        auto en_av = put_string(enum_name);
        auto vr_av = put_string(variant);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::EnumLit));
        put(map_off, ek::ENUM_NAME, en_av);
        put(map_off, ek::VARIANT,   vr_av);
        put(map_off, ek::DISC,      put_i64(disc));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    template <class ExprVec>
    const uint8_t* emit_enum_lit_data_direct(TypeRef ty,
                                                      std::string_view enum_name,
                                                      std::string_view variant,
                                                      int64_t disc,
                                                      const ExprVec& payload) {
        auto en_av = put_string(enum_name);
        auto vr_av = put_string(variant);
        auto pl_av = expr_array(payload);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::EnumLitData));
        put(map_off, ek::ENUM_NAME, en_av);
        put(map_off, ek::VARIANT,   vr_av);
        put(map_off, ek::DISC,      put_i64(disc));
        put(map_off, ek::PAYLOAD,   pl_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    template <class FieldVec>
    const uint8_t* emit_struct_lit_direct(TypeRef ty,
                                                   std::string_view name,
                                                   const FieldVec& fields) {
        auto n_av = put_string(name);
        auto fa   = struct_fields(fields);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::StructLit));
        put(map_off, ek::STRUCT_NAME,  n_av);
        put(map_off, ek::FIELD_NAMES,  fa.names);
        put(map_off, ek::FIELD_VALUES, fa.values);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    template <class ExprVec>
    const uint8_t* emit_call_direct(TypeRef ty,
                                             std::string_view callee,
                                             const std::vector<TypeRef>& type_args,
                                             const ExprVec& args) {
        auto cn_av = put_string(callee);
        auto ta_av = type_array(type_args);
        auto ar_av = expr_array(args);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::Call));
        put(map_off, ek::CALLEE,    cn_av);
        put(map_off, ek::TYPE_ARGS, ta_av);
        put(map_off, ek::ARGS,      ar_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    template <class ExprVec>
    const uint8_t* emit_method_call_direct(TypeRef ty,
                                                    lir_view::ExprRef receiver,
                                                    std::string_view method,
                                                    std::string_view resolved_symbol,
                                                    const std::vector<TypeRef>& type_args,
                                                    const ExprVec& args,
                                                    int32_t vtable_index,
                                                    std::string_view resolved_type,
                                                    std::string_view tag_system,
                                                    std::string_view tag_trait) {
        auto recv_av = expr_av(receiver);
        auto m_av    = put_string(method);
        auto ta_av   = type_array(type_args);
        auto ar_av   = expr_array(args);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::MethodCall));
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
    const uint8_t* emit_unary_direct(TypeRef ty, std::string_view op,
                                              lir_view::ExprRef operand) {
        auto op_av = put_string(op);
        auto o_av  = expr_av(operand);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::Unary));
        put(map_off, ek::OP,      op_av);
        put(map_off, ek::OPERAND, o_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_bin_op_direct(TypeRef ty, std::string_view op,
                                               lir_view::ExprRef lhs,
                                               lir_view::ExprRef rhs) {
        auto op_av = put_string(op);
        auto l_av  = expr_av(lhs);
        auto r_av  = expr_av(rhs);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::BinOp));
        put(map_off, ek::OP,  op_av);
        put(map_off, ek::LHS, l_av);
        put(map_off, ek::RHS, r_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_field_read_direct(TypeRef ty,
                                                   lir_view::ExprRef receiver,
                                                   std::string_view field) {
        auto r_av = expr_av(receiver);
        auto f_av = put_string(field);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::FieldRead));
        put(map_off, ek::RECEIVER, r_av);
        put(map_off, ek::NAME,     f_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_index_read_direct(TypeRef ty,
                                                   lir_view::ExprRef receiver,
                                                   lir_view::ExprRef index) {
        auto r_av = expr_av(receiver);
        auto i_av = expr_av(index);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::IndexRead));
        put(map_off, ek::RECEIVER, r_av);
        put(map_off, ek::INDEX,    i_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_ptr_arith_direct(TypeRef ty, uint8_t op,
                                                  lir_view::ExprRef ptr,
                                                  lir_view::ExprRef offset) {
        auto p_av = expr_av(ptr);
        auto o_av = expr_av(offset);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::PtrArith));
        put(map_off, ek::PTR_ARITH_OP, put_u8(op));
        put(map_off, ek::BASE_PTR,     p_av);
        put(map_off, ek::OFFSET,       o_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_ptr_diff_direct(TypeRef ty, bool by_byte,
                                                 lir_view::ExprRef lhs,
                                                 lir_view::ExprRef rhs) {
        auto l_av = expr_av(lhs);
        auto r_av = expr_av(rhs);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::PtrDiff));
        put(map_off, pdk::BY_BYTE, put_bool(by_byte));
        put(map_off, ek::LHS,      l_av);
        put(map_off, ek::RHS,      r_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_if_expr_direct(TypeRef ty,
                                                lir_view::ExprRef cond,
                                                lir_view::ExprRef then_val,
                                                lir_view::ExprRef else_val) {
        auto c_av = expr_av(cond);
        auto t_av = expr_av(then_val);
        auto e_av = expr_av(else_val);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::IfExpr));
        put(map_off, ek::COND,     c_av);
        put(map_off, ek::THEN_VAL, t_av);
        put(map_off, ek::ELSE_VAL, e_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_tuple_lit_direct(TypeRef ty,
                                         const std::vector<lir_view::ExprRef>& elems) {
        auto el_av = expr_array(elems);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::TupleLit));
        put(map_off, ek::ELEMS, el_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_tuple_index_direct(TypeRef ty,
                                                    lir_view::ExprRef receiver,
                                                    uint32_t index) {
        auto r_av = expr_av(receiver);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::TupleIndex));
        put(map_off, ek::RECEIVER,        r_av);
        put(map_off, ek::TUPLE_INDEX_VAL, put_u32(index));
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    template <class ExprVec>
    const uint8_t* emit_arr_lit_direct(TypeRef ty,
                                                const ExprVec& elems) {
        auto el_av = expr_array(elems);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::ArrLit));
        put(map_off, ek::ELEMS, el_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_block_expr_direct(TypeRef ty,
                                                   lir_view::BlockRef block,
                                                   lir_view::ExprRef result) {
        auto b_av = block ? mref_addr(block.addr()) : writ::AnyVal{};
        auto r_av = expr_av(result);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::BlockExpr));
        put(map_off, ek::BLOCK,  b_av);
        put(map_off, ek::RESULT, r_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    template <class ExprVec>
    const uint8_t* emit_closure_call_direct(TypeRef ty,
                                                     lir_view::ExprRef callee,
                                                     const ExprVec& args) {
        auto c_av = expr_av(callee);
        auto a_av = expr_array(args);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::ClosureCall));
        put(map_off, ek::CALLEE, c_av);
        put(map_off, ek::ARGS,   a_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    template <class ExprVec>
    const uint8_t* emit_fn_ptr_call_direct(TypeRef ty,
                                                    lir_view::ExprRef callee,
                                                    const ExprVec& args) {
        auto c_av = expr_av(callee);
        auto a_av = expr_array(args);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::FnPtrCall));
        put(map_off, ek::CALLEE, c_av);
        put(map_off, ek::ARGS,   a_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_match_expr_direct(TypeRef ty,
                                                   lir_view::ExprRef scrut,
                                                   const std::vector<lir::EMatchArm>& arms) {
        auto sc_av = expr_av(scrut);
        auto ar_av = expr_arm_array(arms);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::MatchExpr));
        put(map_off, ek::SCRUT, sc_av);
        put(map_off, ek::ARMS,  ar_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_match_expr_direct(TypeRef ty,
                                                   lir_view::ExprRef scrut,
                                                   const std::vector<lir::EMatchArmView>& arms) {
        auto sc_av = expr_av(scrut);
        auto ar_av = expr_arm_array(arms);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::MatchExpr));
        put(map_off, ek::SCRUT, sc_av);
        put(map_off, ek::ARMS,  ar_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    template <class ExprVec>
    const uint8_t* emit_format_call_direct(TypeRef ty,
                                                    lir_view::ExprRef fmt,
                                                    const ExprVec& args,
                                                    const std::vector<TypeRef>& arg_types) {
        auto f_av  = expr_av(fmt);
        auto a_av  = expr_array(args);
        auto at_av = type_array(arg_types);
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::FormatCall));
        put(map_off, ek::FMT,       f_av);
        put(map_off, ek::ARGS,      a_av);
        put(map_off, ek::ARG_TYPES, at_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }
    const uint8_t* emit_closure_box_direct(TypeRef ty,
                                                    const lir::EClosure* inner) {
        // Closure body / captures live inside `inner`; emit_closure walks the
        // sub-tree (block + captures + params) and yields the closure's mirror
        // offset, which we attach as ek::CLOSURE.
        auto cl_av = inner ? closure_av(*inner) : writ::AnyVal{};
        auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::ClosureBox));
        put(map_off, ek::CLOSURE, cl_av);
        if (ty) put(map_off, ec::TYPE, type_av(ty));
        return map_off;
    }

    // Stage B.6 — LStmt direct mirror writers. Mirror the per-kind branches of
    // emit_stmt's std::visit but allocate from primitive args, never reading
    // LStmt::kind.
    void put_line(const uint8_t* map_off, uint32_t line) {
        if (line != 0) put(map_off, sc::LINE, put_u32(line));
    }
    const uint8_t* emit_let_direct(uint32_t line, std::string_view name,
                                            TypeRef ty, lir_view::ExprRef value,
                                            bool is_mut, uint32_t slot = 0xFFFFFFFFu,
                                            bool compiler_glue = false) {
        auto name_av = put_string(name);
        auto val_av  = expr_av(value);
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::Let));
        put(map_off, sk::NAME,   name_av);
        put(map_off, sk::TYPE,   type_av(ty));
        put(map_off, sk::VALUE,  val_av);
        put(map_off, sk::IS_MUT, put_bool(is_mut));
        if (slot != 0xFFFFFFFFu) put(map_off, sk::VAR_SLOT, put_i64((int64_t)slot));
        if (compiler_glue) put(map_off, sk::COMPILER_GLUE, put_bool(true));
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_assign_direct(uint32_t line, std::string_view name,
                                               lir_view::ExprRef value,
                                               bool drop_old = false) {
        auto name_av = put_string(name);
        auto val_av  = expr_av(value);
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::Assign));
        put(map_off, sk::NAME,  name_av);
        put(map_off, sk::VALUE, val_av);
        if (drop_old) put(map_off, sk::DROP_OLD, put_bool(true));
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_return_direct(uint32_t line, lir_view::ExprRef value) {
        auto val_av = expr_av(value);
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::Return));
        put(map_off, sk::VALUE, val_av);
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_if_stmt_direct(uint32_t line,
                                                lir_view::ExprRef cond,
                                                lir_view::BlockRef then_blk,
                                                lir_view::BlockRef else_blk) {
        auto cond_av = expr_av(cond);
        auto then_av = then_blk ? mref_addr(then_blk.addr()) : writ::AnyVal{};
        writ::AnyVal else_av;
        if (else_blk) else_av = mref_addr(else_blk.addr());
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::If));
        put(map_off, sk::COND,       cond_av);
        put(map_off, sk::THEN_BLOCK, then_av);
        put(map_off, sk::ELSE_BLOCK, else_av);
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_while_direct(uint32_t line,
                                              lir_view::ExprRef cond,
                                              lir_view::BlockRef body,
                                              std::string_view label) {
        auto cond_av = expr_av(cond);
        auto body_av = body ? mref_addr(body.addr()) : writ::AnyVal{};
        writ::AnyVal label_av;
        if (!label.empty()) label_av = put_string(label);
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::While));
        put(map_off, sk::COND,  cond_av);
        put(map_off, sk::BODY,  body_av);
        put(map_off, sk::LABEL, label_av);
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_for_direct(uint32_t line,
                                            std::string_view var,
                                            lir_view::ExprRef lo,
                                            lir_view::ExprRef hi,
                                            bool inclusive,
                                            lir_view::BlockRef body,
                                            std::string_view label,
                                            uint32_t slot = 0xFFFFFFFFu) {
        auto var_av  = put_string(var);
        auto lo_av   = expr_av(lo);
        auto hi_av   = expr_av(hi);
        auto body_av = body ? mref_addr(body.addr()) : writ::AnyVal{};
        writ::AnyVal label_av;
        if (!label.empty()) label_av = put_string(label);
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::For));
        put(map_off, sk::VAR,       var_av);
        put(map_off, sk::LO,        lo_av);
        put(map_off, sk::HI,        hi_av);
        put(map_off, sk::INCLUSIVE, put_bool(inclusive));
        put(map_off, sk::BODY,      body_av);
        put(map_off, sk::LABEL,     label_av);
        if (slot != 0xFFFFFFFFu) put(map_off, sk::VAR_SLOT, put_i64((int64_t)slot));
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_loop_direct(uint32_t line,
                                             lir_view::BlockRef body,
                                             std::string_view label,
                                             std::string_view break_slot,
                                             TypeRef result_type) {
        auto body_av = body ? mref_addr(body.addr()) : writ::AnyVal{};
        writ::AnyVal label_av, slot_av;
        if (!label.empty())      label_av = put_string(label);
        if (!break_slot.empty()) slot_av  = put_string(break_slot);
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::Loop));
        put(map_off, sk::BODY,        body_av);
        put(map_off, sk::LABEL,       label_av);
        put(map_off, sk::BREAK_SLOT,  slot_av);
        put(map_off, sk::RESULT_TYPE, type_av(result_type));
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_break_direct(uint32_t line,
                                              lir_view::ExprRef value,
                                              std::string_view label) {
        auto val_av = expr_av(value);
        writ::AnyVal label_av;
        if (!label.empty()) label_av = put_string(label);
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::Break));
        put(map_off, sk::VALUE, val_av);
        put(map_off, sk::LABEL, label_av);
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_continue_direct(uint32_t line,
                                                 std::string_view label) {
        writ::AnyVal label_av;
        if (!label.empty()) label_av = put_string(label);
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::Continue));
        put(map_off, sk::LABEL, label_av);
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_block_stmt_direct(uint32_t line,
                                                   lir_view::BlockRef body,
                                                   bool transparent) {
        auto body_av = body ? mref_addr(body.addr()) : writ::AnyVal{};
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::Block));
        put(map_off, sk::BODY, body_av);
        // Carried, never re-derived — see stmt_keys::TRANSPARENT.
        if (transparent) put(map_off, sk::TRANSPARENT, put_bool(true));
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_field_write_direct(uint32_t line,
                                                    std::string_view receiver,
                                                    std::string_view field,
                                                    lir_view::ExprRef value) {
        auto recv_av  = put_string(receiver);
        auto field_av = put_string(field);
        auto val_av   = expr_av(value);
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::FieldWrite));
        put(map_off, sk::RECEIVER, recv_av);
        put(map_off, sk::FIELD,    field_av);
        put(map_off, sk::VALUE,    val_av);
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_index_write_direct(uint32_t line,
                                                    std::string_view arr,
                                                    lir_view::ExprRef index,
                                                    lir_view::ExprRef value) {
        auto arr_av  = put_string(arr);
        auto idx_av  = expr_av(index);
        auto val_av  = expr_av(value);
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::IndexWrite));
        put(map_off, sk::NAME,  arr_av);
        put(map_off, sk::INDEX, idx_av);
        put(map_off, sk::VALUE, val_av);
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_field_index_write_direct(uint32_t line,
                                                          std::string_view receiver,
                                                          std::string_view field,
                                                          lir_view::ExprRef index,
                                                          lir_view::ExprRef value) {
        auto recv_av  = put_string(receiver);
        auto field_av = put_string(field);
        auto idx_av   = expr_av(index);
        auto val_av   = expr_av(value);
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::FieldIndexWrite));
        put(map_off, sk::RECEIVER, recv_av);
        put(map_off, sk::FIELD,    field_av);
        put(map_off, sk::INDEX,    idx_av);
        put(map_off, sk::VALUE,    val_av);
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_expr_stmt_direct(uint32_t line,
                                                  lir_view::ExprRef expr) {
        auto expr_avv = expr_av(expr);
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::ExprStmt));
        put(map_off, sk::EXPR, expr_avv);
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_match_stmt_direct(uint32_t line,
                                                   lir_view::ExprRef scrut,
                                                   const std::vector<lir::LMatchArm>& arms) {
        auto scrut_av = expr_av(scrut);
        auto arms_av  = arm_array(arms);
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::Match));
        put(map_off, sk::SCRUT, scrut_av);
        put(map_off, sk::ARMS,  arms_av);
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_for_each_direct(uint32_t line,
                                                 std::string_view var,
                                                 lir_view::ExprRef iter,
                                                 TypeRef elem_type,
                                                 int64_t arr_size,
                                                 bool is_slice,
                                                 lir_view::BlockRef body,
                                                 uint32_t slot = 0xFFFFFFFFu,
                                                 bool var_mut = false) {
        auto var_av  = put_string(var);
        auto iter_av = expr_av(iter);
        auto body_av = body ? mref_addr(body.addr()) : writ::AnyVal{};
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::ForEach), 9);  // + sk::IS_MUT
        put(map_off, sk::VAR,       var_av);
        put(map_off, sk::ITER,      iter_av);
        put(map_off, sk::ELEM_TYPE, type_av(elem_type));
        put(map_off, sk::ARR_SIZE,  put_i64(arr_size));
        put(map_off, sk::IS_SLICE,  put_bool(is_slice));
        put(map_off, sk::BODY,      body_av);
        if (slot != 0xFFFFFFFFu) put(map_off, sk::VAR_SLOT, put_i64((int64_t)slot));
        if (var_mut) put(map_off, sk::IS_MUT, put_bool(true));  // sparse: `for mut x`
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_deref_write_direct(uint32_t line,
                                                    lir_view::ExprRef ptr,
                                                    lir_view::ExprRef value,
                                                    bool drop_old = false) {
        auto ptr_av = expr_av(ptr);
        auto val_av = expr_av(value);
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::DerefWrite));
        put(map_off, sk::PTR,   ptr_av);
        put(map_off, sk::VALUE, val_av);
        if (drop_old) put(map_off, sk::DROP_OLD, put_bool(true));
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_drop_direct(uint32_t line,
                                             std::string_view var_name,
                                             std::string_view drop_fn,
                                             TypeRef ty,
                                             bool drop_fields,
                                             const std::vector<std::string>& moved_fields) {
        auto var_av = put_string(var_name);
        writ::AnyVal drop_av;
        if (!drop_fn.empty()) drop_av = put_string(drop_fn);
        writ::AnyVal moved_av;
        if (!moved_fields.empty()) moved_av = string_array(moved_fields);
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::Drop));
        put(map_off, sk::NAME,         var_av);
        put(map_off, sk::DROP_FN,      drop_av);
        put(map_off, sk::TYPE,         type_av(ty));
        put(map_off, sk::DROP_FIELDS,  put_bool(drop_fields));
        if (!moved_fields.empty())
            put(map_off, sk::MOVED_FIELDS, moved_av);
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_deref_field_write_direct(uint32_t line,
                                                          std::string_view receiver,
                                                          std::string_view type_name,
                                                          std::string_view field,
                                                          lir_view::ExprRef value) {
        auto recv_av  = put_string(receiver);
        auto type_avv = put_string(type_name);
        auto field_av = put_string(field);
        auto val_av   = expr_av(value);
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::DerefFieldWrite));
        put(map_off, sk::RECEIVER,   recv_av);
        put(map_off, sk::TYPE_NAME,  type_avv);
        put(map_off, sk::FIELD,      field_av);
        put(map_off, sk::VALUE,      val_av);
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_tuple_write_direct(uint32_t line,
                                                    std::string_view receiver,
                                                    uint32_t index,
                                                    lir_view::ExprRef value,
                                                    TypeRef recv_type) {
        auto recv_av = put_string(receiver);
        auto val_av  = expr_av(value);
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::TupleWrite));
        put(map_off, sk::RECEIVER,        recv_av);
        put(map_off, sk::TUPLE_INDEX_VAL, put_u32(index));
        put(map_off, sk::VALUE,           val_av);
        put(map_off, sk::RECV_TYPE,       type_av(recv_type));
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_let_else_direct(uint32_t line,
                                                 const lir::Pattern& pat,
                                                 lir_view::ExprRef scrut,
                                                 lir_view::BlockRef else_block,
                                                 const std::vector<lir::LExprPtr>& guards) {
        auto pat_off  = emit_pat(pat);
        auto scrut_av = expr_av(scrut);
        auto eb_av    = else_block ? mref_addr(else_block.addr()) : writ::AnyVal{};
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::LetElse));
        put(map_off, sk::PAT,           mref_addr(pat_off));
        put(map_off, sk::SCRUT,         scrut_av);
        put(map_off, sk::ELSE_DIVERGE,  eb_av);
        if (!guards.empty())
            put(map_off, sk::LET_ELSE_GUARDS, expr_array(guards));   // G161-3
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_chain_field_write_direct(uint32_t line,
                                                          std::string_view receiver,
                                                          std::string_view mid_field,
                                                          const std::vector<std::string>& extras,
                                                          std::string_view field,
                                                          lir_view::ExprRef value) {
        auto recv_av   = put_string(receiver);
        auto mid_av    = put_string(mid_field);
        auto extras_av = string_array(extras);
        auto fld_av    = put_string(field);
        auto val_av    = expr_av(value);
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::ChainFieldWrite));
        put(map_off, sk::RECEIVER,   recv_av);
        put(map_off, sk::MID_FIELD,  mid_av);
        if (!extras.empty())
            put(map_off, sk::EXTRA_MIDS, extras_av);
        put(map_off, sk::FIELD,      fld_av);
        put(map_off, sk::VALUE,      val_av);
        put_line(map_off, line);
        return map_off;
    }

    // ── Stage B.6 — WritVal direct mirror writers ─────────────────────────
    // Each mirrors the corresponding branch of emit_hv's std::visit but
    // allocates from primitive args, never reading WritVal::kind. Children
    // (WritValPtr) must already have their own mirror_ptr_; hv_av will
    // back-fill the cache via the field-as-truth path.
    static constexpr int32_t WV_BASE_DIRECT = 200;

    const uint8_t* emit_hv_null_direct() {
        return make_map(writ::schema::lir_expr(WV_BASE_DIRECT + 0));
    }
    const uint8_t* emit_hv_bool_direct(bool value) {
        auto map_off = make_map(writ::schema::lir_expr(WV_BASE_DIRECT + 1));
        put(map_off, hk::BOOL_VALUE, put_bool(value));
        return map_off;
    }
    const uint8_t* emit_hv_int_direct(int64_t value) {
        auto map_off = make_map(writ::schema::lir_expr(WV_BASE_DIRECT + 2));
        put(map_off, hk::INT_VALUE, put_i64(value));
        return map_off;
    }
    const uint8_t* emit_hv_float_direct(double value) {
        auto map_off = make_map(writ::schema::lir_expr(WV_BASE_DIRECT + 3));
        put(map_off, hk::FLOAT_VALUE, put_f64(value));
        return map_off;
    }
    const uint8_t* emit_hv_str_direct(std::string_view value) {
        auto s_av = put_string(value);
        auto map_off = make_map(writ::schema::lir_expr(WV_BASE_DIRECT + 4));
        put(map_off, hk::STR_VALUE, s_av);
        return map_off;
    }
    const uint8_t* emit_hv_map_direct(const std::vector<lir::WVMapEntry>& entries,
                                               std::string_view key_type) {
        std::vector<writ::AnyVal> key_strs, key_ints, val_avs;
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
        writ::AnyVal keys_av;
        if (!key_strs.empty()) {
            auto off = make_array(key_strs.size());
            for (auto av : key_strs) array_push(off, av);
            keys_av = mref_addr(off);
        } else if (!key_ints.empty()) {
            auto off = make_array(key_ints.size());
            for (auto av : key_ints) array_push(off, av);
            keys_av = mref_addr(off);
        }
        writ::AnyVal vals_av;
        if (!val_avs.empty()) {
            auto off = make_array(val_avs.size());
            for (auto av : val_avs) array_push(off, av);
            vals_av = mref_addr(off);
        }
        auto map_off = make_map(writ::schema::lir_expr(WV_BASE_DIRECT + 5));
        put(map_off, hk::MAP_KEYS,   keys_av);
        put(map_off, hk::MAP_VALUES, vals_av);
        if (!key_type.empty())
            put(map_off, hk::TYPE_NAME, put_string(key_type));
        return map_off;
    }
    const uint8_t* emit_hv_array_direct(const std::vector<lir::WritValPtr>& elements,
                                                 std::string_view elem_type) {
        std::vector<writ::AnyVal> elems;
        elems.reserve(elements.size());
        for (auto& e : elements) elems.push_back(hv_av(e));
        writ::AnyVal arr_av;
        if (!elems.empty()) {
            auto off = make_array(elems.size());
            for (auto av : elems) array_push(off, av);
            arr_av = mref_addr(off);
        }
        auto map_off = make_map(writ::schema::lir_expr(WV_BASE_DIRECT + 6));
        put(map_off, hk::ELEMS, arr_av);
        if (!elem_type.empty())
            put(map_off, hk::TYPE_NAME, put_string(elem_type));
        return map_off;
    }
    const uint8_t* emit_hv_capture_direct(uint32_t param_index, uint32_t value_index) {
        auto map_off = make_map(writ::schema::lir_expr(WV_BASE_DIRECT + 7));
        put(map_off, hk::PARAM_INDEX, put_u32(param_index));
        put(map_off, hk::VALUE_INDEX, put_u32(value_index));
        return map_off;
    }
    const uint8_t* emit_hv_type_direct(uint32_t kind, uint64_t uid, std::string_view name) {
        auto name_av = put_string(name);
        auto map_off = make_map(writ::schema::lir_expr(WV_BASE_DIRECT + 8));
        put(map_off, hk::STR_VALUE, name_av);
        put(map_off, hk::TYPE_KIND, put_u32(kind));
        put(map_off, hk::TYPE_UID,  put_u64(uid));
        return map_off;
    }

    // Stage B.6 — Pattern direct mirror writers. Mirror the per-kind branches
    // of emit_pat's std::visit but allocate from primitive args, never reading
    // Pattern::kind. Sub-pattern children must already carry their own
    // mirror_ptr_; pat_array recurses through the cache-hit fast path.
    const uint8_t* emit_pat_variant_direct(std::string_view enum_name,
                                                    std::string_view variant,
                                                    int64_t disc) {
        auto enum_av    = put_string(enum_name);
        auto variant_av = put_string(variant);
        auto map_off = make_map(writ::schema::lir_pat(lir_schema::pat::Code::Variant));
        put(map_off, pk::ENUM_NAME, enum_av);
        put(map_off, pk::VARIANT,   variant_av);
        put(map_off, pk::DISC,      put_i64(disc));
        return map_off;
    }
    const uint8_t* emit_pat_int_direct(int64_t value) {
        auto map_off = make_map(writ::schema::lir_pat(lir_schema::pat::Code::Int));
        put(map_off, pk::INT_VALUE, put_i64(value));
        return map_off;
    }
    const uint8_t* emit_pat_bool_direct(bool value) {
        auto map_off = make_map(writ::schema::lir_pat(lir_schema::pat::Code::Bool));
        put(map_off, pk::BOOL_VALUE, put_bool(value));
        return map_off;
    }
    const uint8_t* emit_pat_wild_direct(std::string_view name,
                                                uint32_t slot = 0xFFFFFFFFu,
                                                bool is_mut = false) {
        auto name_av = name.empty() ? writ::AnyVal{} : put_string(name);
        auto map_off = make_map(writ::schema::lir_pat(lir_schema::pat::Code::Wild));
        put(map_off, pk::NAME, name_av);
        if (is_mut) put(map_off, pk::IS_MUT, put_bool(true));  // sparse: by-value `mut x`
        if (slot != 0xFFFFFFFFu) put(map_off, pk::BIND_SLOT, put_i64((int64_t)slot));
        return map_off;
    }
    const uint8_t* emit_pat_variant_data_direct(std::string_view enum_name,
                                                         std::string_view variant,
                                                         int64_t disc,
                                                         const std::vector<std::string>& bindings,
                                                         const std::vector<TypeRef>& binding_types,
                                                         const std::vector<uint32_t>& bind_slots = {},
                                                         const std::vector<uint32_t>& bind_ref_modes = {}) {
        auto enum_av     = put_string(enum_name);
        auto variant_av  = put_string(variant);
        auto bindings_av = string_array(bindings);
        auto btypes_av   = type_array(binding_types);
        auto slots_av    = u32_array(bind_slots);
        // Written only when some binding is by-reference: an all-by-value
        // pattern emits byte-identical mirror bytes to the pre-key emitter.
        bool any_ref = false;
        for (auto m : bind_ref_modes) if (m != 0) { any_ref = true; break; }
        auto modes_av    = any_ref ? u32_array(bind_ref_modes) : writ::AnyVal{};
        auto map_off = make_map(writ::schema::lir_pat(lir_schema::pat::Code::VariantData));
        put(map_off, pk::ENUM_NAME,      enum_av);
        put(map_off, pk::VARIANT,        variant_av);
        put(map_off, pk::DISC,           put_i64(disc));
        put(map_off, pk::BINDINGS,       bindings_av);
        put(map_off, pk::BINDING_TYPES,  btypes_av);
        if (!slots_av.is_null()) put(map_off, pk::BIND_SLOTS, slots_av);
        if (!modes_av.is_null()) put(map_off, pk::BINDING_REF_MODES, modes_av);
        return map_off;
    }
    const uint8_t* emit_pat_or_direct(const std::vector<lir::Pattern>& alts) {
        auto subs_av = pat_array(alts);
        auto map_off = make_map(writ::schema::lir_pat(lir_schema::pat::Code::Or));
        put(map_off, pk::SUBS, subs_av);
        return map_off;
    }
    const uint8_t* emit_pat_tuple_direct(const std::vector<std::string>& bindings,
                                                  const std::vector<TypeRef>& binding_types,
                                                  const std::vector<lir::Pattern>& subs,
                                                  const std::vector<uint32_t>& bind_slots = {}) {
        auto bindings_av = string_array(bindings);
        auto btypes_av   = type_array(binding_types);
        auto subs_av     = pat_array(subs);
        auto slots_av    = u32_array(bind_slots);
        auto map_off = make_map(writ::schema::lir_pat(lir_schema::pat::Code::Tuple));
        put(map_off, pk::BINDINGS,      bindings_av);
        put(map_off, pk::BINDING_TYPES, btypes_av);
        put(map_off, pk::SUBS,          subs_av);
        if (!slots_av.is_null()) put(map_off, pk::BIND_SLOTS, slots_av);
        return map_off;
    }
    const uint8_t* emit_pat_range_direct(__int128 lo, __int128 hi) {
        auto map_off = make_map(writ::schema::lir_pat(lir_schema::pat::Code::Range));
        // The high half is written only when it carries information the sign
        // extension of the low half does not — so a sub-128-bit bound produces
        // byte-identical mirror bytes to the pre-widening emitter.
        auto put_half = [&](const lir_schema::Key& k, const lir_schema::Key& k_hi, __int128 v) {
            int64_t low = (int64_t)(uint64_t)(unsigned __int128)v;
            int64_t high = (int64_t)(uint64_t)((unsigned __int128)v >> 64);
            put(map_off, k, put_i64(low));
            if (high != (low >> 63)) put(map_off, k_hi, put_i64(high));
        };
        put_half(pk::LO, pk::LO_HI, lo);
        put_half(pk::HI, pk::HI_HI, hi);
        return map_off;
    }
    const uint8_t* emit_pat_struct_direct(std::string_view struct_name,
                                                   const std::vector<lir::PatFieldBinding>& fields,
                                                   bool has_rest) {
        auto name_av   = put_string(struct_name);
        auto fields_av = field_binding_array(fields);
        auto map_off = make_map(writ::schema::lir_pat(lir_schema::pat::Code::Struct));
        put(map_off, pk::STRUCT_NAME, name_av);
        put(map_off, pk::FIELDS,      fields_av);
        put(map_off, pk::HAS_REST,    put_bool(has_rest));
        return map_off;
    }
    const uint8_t* emit_pat_slice_direct(const std::vector<lir::Pattern>& prefix,
                                                  const std::vector<lir::Pattern>& rest,
                                                  const std::vector<lir::Pattern>& suffix) {
        auto pre_av  = pat_array(prefix);
        auto rest_av = pat_array(rest);
        auto suf_av  = pat_array(suffix);
        auto map_off = make_map(writ::schema::lir_pat(lir_schema::pat::Code::Slice));
        put(map_off, pk::PREFIX, pre_av);
        put(map_off, pk::REST,   rest_av);
        put(map_off, pk::SUFFIX, suf_av);
        return map_off;
    }
    const uint8_t* emit_pat_at_direct(std::string_view name,
                                               const std::vector<lir::Pattern>& sub,
                                               TypeRef type, uint32_t slot = 0xFFFFFFFFu) {
        auto name_av = put_string(name);
        auto sub_av  = pat_array(sub);
        auto map_off = make_map(writ::schema::lir_pat(lir_schema::pat::Code::At));
        put(map_off, pk::NAME, name_av);
        put(map_off, pk::SUB,  sub_av);
        put(map_off, pk::TYPE, type_av(type));
        if (slot != 0xFFFFFFFFu) put(map_off, pk::BIND_SLOT, put_i64((int64_t)slot));
        return map_off;
    }
    const uint8_t* emit_pat_ref_bind_direct(std::string_view name,
                                                     bool is_mut,
                                                     TypeRef bind_type, uint32_t slot = 0xFFFFFFFFu) {
        auto name_av = put_string(name);
        auto map_off = make_map(writ::schema::lir_pat(lir_schema::pat::Code::RefBind));
        put(map_off, pk::NAME,      name_av);
        put(map_off, pk::IS_MUT,    put_bool(is_mut));
        put(map_off, pk::BIND_TYPE, type_av(bind_type));
        if (slot != 0xFFFFFFFFu) put(map_off, pk::BIND_SLOT, put_i64((int64_t)slot));
        return map_off;
    }
    const uint8_t* emit_pat_ref_pat_direct(const std::vector<lir::Pattern>& inner,
                                                    bool is_mut) {
        auto inner_av = pat_array(inner);
        auto map_off = make_map(writ::schema::lir_pat(lir_schema::pat::Code::RefPat));
        put(map_off, pk::INNER,  inner_av);
        put(map_off, pk::IS_MUT, put_bool(is_mut));
        return map_off;
    }

public:
    // ── primitive helpers (exposed for DeclBuilder direct-build factory) ─────
    // Member fields stay private (declared above the first `public:`).

    // Raw arena access is encapsulated in views. The view re-resolves obj from
    // the offset against the holder's base on construction, so it stays valid
    // across reallocs as long as it isn't held past the next allocation (same
    // discipline as the old raw refetch — Stage C / MultiChunk removes it).
    writ::TinyMapView tom_at(const uint8_t* addr) noexcept {
        return writ::TinyMapView(
            reinterpret_cast<writ::TinyObjectMap*>(const_cast<uint8_t*>(addr)),
            holder());
    }
    writ::ArrayView arr_at(const uint8_t* addr) noexcept {
        return writ::ArrayView(
            reinterpret_cast<writ::ObjectArray*>(const_cast<uint8_t*>(addr)),
            holder());
    }

    writ::AnyVal put_string(std::string_view s) {
        auto v = ctr_.make_string(s);
        LOGOS_ASSERT(v.has_value(), "LIR-MIRROR-001", "ArenaString alloc failed");
        return v->to_anyval();
    }
    writ::AnyVal put_i64(int64_t v) {
        auto av = ctr_.box<int64_t>(v);
        LOGOS_ASSERT(av.has_value(), "LIR-MIRROR-002", "i64 anyval put failed");
        return *av;
    }
    writ::AnyVal put_u64(uint64_t v) {
        auto av = ctr_.box<uint64_t>(v);
        LOGOS_ASSERT(av.has_value(), "LIR-MIRROR-002", "u64 anyval put failed");
        return *av;
    }
    writ::AnyVal put_f64(double v) {
        auto av = ctr_.box<double>(v);
        LOGOS_ASSERT(av.has_value(), "LIR-MIRROR-002", "f64 anyval put failed");
        return *av;
    }
    writ::AnyVal put_bool(bool v) {
        return writ::AnyVal::from_value<uint8_t>(
            v ? 1 : 0, writ::type_hash::Bool);
    }
    writ::AnyVal put_i32(int32_t v) {
        return writ::AnyVal::from_value<int32_t>(v);
    }
    writ::AnyVal put_u32(uint32_t v) {
        return writ::AnyVal::from_value<uint32_t>(v);
    }
    writ::AnyVal put_u8(uint8_t v) {
        return writ::AnyVal::from_value<uint8_t>(v);
    }

    writ::AnyVal type_av(TypeRef t) {
        if (!t) return writ::AnyVal{};
        // Phase 5.B step 3: foreign TypeRef → intern into local pool before
        // referencing it. The mirror's TYPE field is interpreted in the local
        // arena, so a foreign reference here would read garbage later.
        // is_external() is a single uint32 compare on the hot local path.
        if (pool_ && t.is_external()) t = pool_->intern_foreign(t);
        return mref_addr(t.addr());
    }

    // ── child-emit helpers (returns AnyVal pointing at child mirror) ───────

    // Stage D: EXPR children are eagerly direct-emitted, so their mirror_ptr_ is
    // already set — reference by address via the view (no emit_expr re-walk).
    writ::AnyVal expr_av(lir_view::ExprRef e)  { return e ? mref_addr(e.addr()) : writ::AnyVal{}; }
    writ::AnyVal pat_av(const Pattern& p) {
        return mref_addr(emit_pat(p));
    }
    writ::AnyVal hv_av(const lir::WritValPtr& v) {
        if (!v) return writ::AnyVal{};
        return mref_addr(emit_hv(*v));
    }
    writ::AnyVal arm_av(const LMatchArm& a) {
        return mref_addr(emit_arm(a));
    }
    writ::AnyVal closure_av(const EClosure& c) {
        return mref_addr(emit_closure(c));
    }

    // ── ObjectArray helpers ────────────────────────────────────────────────

    const uint8_t* make_array(size_t n) {
        if (dry_run_) return nullptr;
        auto arr = ctr_.make_array(n == 0 ? 1 : n);
        LOGOS_ASSERT(arr.has_value(), "LIR-MIRROR-003", "ObjectArray alloc failed");
        return reinterpret_cast<const uint8_t*>(arr->ptr());
    }
    void array_push(const uint8_t* arr_addr, writ::AnyVal v) {
        if (dry_run_) return;
        auto r = arr_at(arr_addr).push_back(v);
        LOGOS_ASSERT(r.has_value(), "LIR-MIRROR-003", "ObjectArray push failed");
    }

    // Stage D: elements as mirror views (the eager-emitted expr mirrors).
    writ::AnyVal expr_array(const std::vector<lir_view::ExprRef>& v) {
        if (v.empty()) return writ::AnyVal{};
        std::vector<writ::AnyVal> elems;
        elems.reserve(v.size());
        for (auto& e : v) elems.push_back(expr_av(e));
        auto arr_off = make_array(elems.size());
        for (auto av : elems) array_push(arr_off, av);
        return mref_addr(arr_off);
    }
    writ::AnyVal type_array(const std::vector<TypeRef>& v) {
        if (v.empty()) return writ::AnyVal{};
        auto arr_off = make_array(v.size());
        for (auto t : v) array_push(arr_off, type_av(t));
        return mref_addr(arr_off);
    }
    writ::AnyVal string_array(const std::vector<std::string>& v) {
        if (v.empty()) return writ::AnyVal{};
        std::vector<writ::AnyVal> elems;
        elems.reserve(v.size());
        for (auto& s : v) elems.push_back(put_string(s));
        auto arr_off = make_array(elems.size());
        for (auto av : elems) array_push(arr_off, av);
        return mref_addr(arr_off);
    }
    writ::AnyVal pat_array(const std::vector<Pattern>& v) {
        if (v.empty()) return writ::AnyVal{};
        std::vector<writ::AnyVal> elems;
        elems.reserve(v.size());
        for (auto& p : v) elems.push_back(pat_av(p));
        auto arr_off = make_array(elems.size());
        for (auto av : elems) array_push(arr_off, av);
        return mref_addr(arr_off);
    }
    writ::AnyVal u32_array(const std::vector<uint32_t>& v) {
        if (v.empty()) return writ::AnyVal{};
        std::vector<writ::AnyVal> elems;
        elems.reserve(v.size());
        for (auto x : v) elems.push_back(put_u32(x));
        auto arr_off = make_array(elems.size());
        for (auto av : elems) array_push(arr_off, av);
        return mref_addr(arr_off);
    }
    writ::AnyVal arm_array(const std::vector<LMatchArm>& v) {
        if (v.empty()) return writ::AnyVal{};
        std::vector<writ::AnyVal> elems;
        elems.reserve(v.size());
        for (auto& a : v) elems.push_back(arm_av(a));
        auto arr_off = make_array(elems.size());
        for (auto av : elems) array_push(arr_off, av);
        return mref_addr(arr_off);
    }

    // EMatchExpr arms have a different shape than LMatchArm (value vs body) —
    // emit each as a small TinyObjectMap and return an array of AnyVal.
    writ::AnyVal expr_arm_array(const std::vector<lir::EMatchArm>& v);
    writ::AnyVal expr_arm_array(const std::vector<lir::EMatchArmView>& v);
    const uint8_t* emit_expr_arm(const lir::EMatchArmView& a);

    // EStructLit fields: emit FIELD_NAMES + FIELD_VALUES parallel arrays
    // and write them to the parent map. Returns the two arrays as a pair.
    struct FieldArrays {
        writ::AnyVal names;
        writ::AnyVal values;
    };
    template <class FieldVec>
    FieldArrays struct_fields(const FieldVec& fields) {
        FieldArrays out;
        if (fields.empty()) return out;
        std::vector<writ::AnyVal> name_elems, value_elems;
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
        out.names  = mref_addr(names_off);
        out.values = mref_addr(values_off);
        return out;
    }

    // PatFieldBinding array (for PatStruct).
    writ::AnyVal field_binding_array(
        const std::vector<lir::PatFieldBinding>& v);

    // ── map creation + put helpers ─────────────────────────────────────────

    const uint8_t* make_map(uint64_t schema_code, uint64_t cap = 8) {
        if (dry_run_) return nullptr;
        auto m = ctr_.make_tiny_map_view(cap);
        LOGOS_ASSERT(m.has_value(), "LIR-MIRROR-004",
            "TinyObjectMap allocation failed");
        m->set_schema_type_code(schema_code);
        return reinterpret_cast<const uint8_t*>(m->ptr());
    }
    void put(const uint8_t* map_addr,
             const lir_schema::Key& key, writ::AnyVal val) {
        if (dry_run_) return;
        if (val.is_null()) return;
        auto r = tom_at(map_addr).put(key.code, val);
        LOGOS_ASSERT(r.has_value(), "LIR-MIRROR-005",
            "TinyObjectMap put failed");
    }

    // ── expression emit ────────────────────────────────────────────────────

    // EAGER block emit: build a block container mirror from an already-emitted
    // stmt-ref vector (the core of emit_block, no husk/cache). Called at each
    // block's completion point via lir_mirror_block.
    const uint8_t* emit_block_stmts(const std::vector<lir_view::StmtRef>& stmts) {
        std::vector<writ::AnyVal> elems; elems.reserve(stmts.size());
        for (auto& s : stmts) elems.push_back(mref_addr(s.addr()));
        writ::AnyVal stmts_av;
        if (!elems.empty()) {
            auto arr_off = make_array(elems.size());
            for (auto av : elems) array_push(arr_off, av);
            stmts_av = mref_addr(arr_off);
        }
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Count));
        if (!stmts_av.is_null()) put(map_off, sk::ARMS, stmts_av);
        return map_off;
    }
    const uint8_t* emit_pat(const Pattern& p);
    const uint8_t* emit_hv(const WritVal& v);
    const uint8_t* emit_arm(const LMatchArm& a);
    const uint8_t* emit_closure(const EClosure& c);
    const uint8_t* emit_expr_arm(const lir::EMatchArm& a);
    const uint8_t* emit_field_binding(const lir::PatFieldBinding& fb);
};

// ──────────────────────────────────────────────────────────────────────────
// Block / function body
// ──────────────────────────────────────────────────────────────────────────

// ──────────────────────────────────────────────────────────────────────────
// LMatchArm / EMatchArm / PatFieldBinding / EClosure
// ──────────────────────────────────────────────────────────────────────────

const uint8_t* LirMirrorEmitter::emit_arm(const LMatchArm& a) {
    auto pat_off    = emit_pat(a.pat);
    auto body_off   = a.body.addr();   // Stage D: arm body pre-emitted BlockRef
    writ::AnyVal guard_av;
    if (a.guard.has_value()) guard_av = expr_av(*a.guard);

    auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Count + 1));
    put(map_off, ak::PAT,   mref_addr(pat_off));
    put(map_off, ak::BODY,  mref_addr(body_off));
    put(map_off, ak::GUARD, guard_av);
    return map_off;
}

const uint8_t* LirMirrorEmitter::emit_expr_arm(const lir::EMatchArm& a) {
    auto pat_off    = emit_pat(a.pat);
    auto value_off  = a.value.addr();
    writ::AnyVal guard_av;
    if (a.guard.has_value()) guard_av = expr_av(*a.guard);

    auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Count + 2));
    put(map_off, ak::PAT,   mref_addr(pat_off));
    put(map_off, ak::VALUE, mref_addr(value_off));
    put(map_off, ak::GUARD, guard_av);
    return map_off;
}

writ::AnyVal LirMirrorEmitter::expr_arm_array(
    const std::vector<lir::EMatchArm>& v)
{
    if (v.empty()) return writ::AnyVal{};
    std::vector<writ::AnyVal> elems;
    elems.reserve(v.size());
    for (auto& a : v) elems.push_back(
        mref_addr(emit_expr_arm(a)));
    auto arr_off = make_array(elems.size());
    for (auto av : elems) array_push(arr_off, av);
    return mref_addr(arr_off);
}

const uint8_t* LirMirrorEmitter::emit_expr_arm(const lir::EMatchArmView& a) {
    auto pat_off    = emit_pat(a.pat);
    auto value_off  = expr_av(a.value);   // already-emitted mirror
    writ::AnyVal guard_av;
    if (a.guard) guard_av = expr_av(a.guard);

    auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Count + 2));
    put(map_off, ak::PAT,   mref_addr(pat_off));
    put(map_off, ak::VALUE, value_off);
    put(map_off, ak::GUARD, guard_av);
    return map_off;
}

writ::AnyVal LirMirrorEmitter::expr_arm_array(
    const std::vector<lir::EMatchArmView>& v)
{
    if (v.empty()) return writ::AnyVal{};
    std::vector<writ::AnyVal> elems;
    elems.reserve(v.size());
    for (auto& a : v) elems.push_back(
        mref_addr(emit_expr_arm(a)));
    auto arr_off = make_array(elems.size());
    for (auto av : elems) array_push(arr_off, av);
    return mref_addr(arr_off);
}

const uint8_t* LirMirrorEmitter::emit_field_binding(
    const lir::PatFieldBinding& fb)
{
    auto name_av = put_string(fb.field_name);
    auto subs_av = pat_array(fb.sub);

    auto map_off = make_map(writ::schema::lir_pat(lir_schema::pat::Count));
    put(map_off, pk::FIELD_NAME, name_av);
    put(map_off, pk::SUB,        subs_av);
    if (fb.slot != 0xFFFFFFFFu) put(map_off, pk::BIND_SLOT, put_i64((int64_t)fb.slot));
    return map_off;
}

writ::AnyVal LirMirrorEmitter::field_binding_array(
    const std::vector<lir::PatFieldBinding>& v)
{
    if (v.empty()) return writ::AnyVal{};
    std::vector<writ::AnyVal> elems;
    elems.reserve(v.size());
    for (auto& fb : v) elems.push_back(
        mref_addr(emit_field_binding(fb)));
    auto arr_off = make_array(elems.size());
    for (auto av : elems) array_push(arr_off, av);
    return mref_addr(arr_off);
}

const uint8_t* LirMirrorEmitter::emit_closure(const EClosure& c) {
    // Body first — Stage D: c.body is a pre-emitted BlockRef.
    auto body_off = c.body.addr();

    // Capture types as type-array
    auto cap_types_av = type_array(c.capture_types);
    auto captures_av  = string_array(c.captures);

    // Param names + types as parallel arrays.
    writ::AnyVal param_names_av, param_types_av;
    if (!c.params.empty()) {
        std::vector<writ::AnyVal> n_elems, t_elems;
        n_elems.reserve(c.params.size());
        t_elems.reserve(c.params.size());
        for (auto& p : c.params) {
            n_elems.push_back(put_string(p.name));
            t_elems.push_back(p.type ? type_av(p.type) : writ::AnyVal{});
        }
        auto n_off = make_array(n_elems.size());
        for (auto av : n_elems) array_push(n_off, av);
        auto t_off = make_array(t_elems.size());
        for (auto av : t_elems) array_push(t_off, av);
        param_names_av = mref_addr(n_off);
        param_types_av = mref_addr(t_off);
    }

    // 10 keys (block, name, cap-types, cap-names, param-names, param-types,
    // ret-type, is-move, as-fn-ptr, mut-captures) — default cap=8 overflows.
    auto map_off = make_map(writ::schema::lir_expr(lir_schema::expr::Code::ClosureBox)
                            | (1ULL << 47),
                            /*cap=*/12);
    put(map_off, ck::BLOCK,         mref_addr(body_off));
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
        std::vector<writ::AnyVal> m_elems;
        m_elems.reserve(c.mut_captures.size());
        for (bool m : c.mut_captures) m_elems.push_back(put_bool(m));
        auto m_off = make_array(m_elems.size());
        for (auto av : m_elems) array_push(m_off, av);
        put(map_off, ck::MUT_CAPTURES, mref_addr(m_off));
    }
    // RFC-2229: per-capture dotted field path. Only emit when at least one path
    // is narrower than its root (else whole-var capture is the implicit default).
    if (c.capture_paths.size() == c.captures.size()) {
        bool any_narrow = false;
        for (size_t i = 0; i < c.captures.size(); ++i)
            if (c.capture_paths[i] != c.captures[i]) { any_narrow = true; break; }
        if (any_narrow) {
            std::vector<writ::AnyVal> p_elems;
            p_elems.reserve(c.capture_paths.size());
            for (auto& p : c.capture_paths) p_elems.push_back(put_string(p));
            auto p_off = make_array(p_elems.size());
            for (auto av : p_elems) array_push(p_off, av);
            put(map_off, ck::CAPTURE_PATHS, mref_addr(p_off));
        }
    }
    // RFC-2229 phase-2: per-capture FIELD type (when narrow). Emit only when at
    // least one entry is non-null (whole-root closures keep schema footprint).
    if (c.capture_field_types.size() == c.captures.size()) {
        bool any_narrow_ty = false;
        for (auto& t : c.capture_field_types) if (t) { any_narrow_ty = true; break; }
        if (any_narrow_ty) {
            auto t_av = type_array(c.capture_field_types);
            if (!t_av.is_null()) put(map_off, ck::CAPTURE_FIELD_TYPES, t_av);
        }
    }
    return map_off;
}

// ──────────────────────────────────────────────────────────────────────────
// WritVal mirror
// ──────────────────────────────────────────────────────────────────────────

const uint8_t* LirMirrorEmitter::emit_hv(const WritVal& v) {
    // B.6 Stage 3.5 step 6: mirror_ptr_ is field-as-truth. All WritVal
    // construction sites (sema alloc_hv_emit, mono clone_hv) eagerly emit
    // and set mirror_ptr_ via per-kind direct emitters; nested children
    // are registered transitively. The bulk std::visit fallback is now
    // unreachable.
    LOGOS_ASSERT(v.mirror_ptr_ != nullptr,
                 "B6.S35.S6",
                 "emit_hv: WritVal reached without mirror_ptr_ set "
                 "(construction site missed direct-emit migration)");
    if (auto it = table_.writ_val.find(&v); it != table_.writ_val.end()) {
        if (it->second == v.mirror_ptr_) return it->second;
        it->second = v.mirror_ptr_;
        return v.mirror_ptr_;
    }
    table_.writ_val[&v] = v.mirror_ptr_;
    return v.mirror_ptr_;
}


// ──────────────────────────────────────────────────────────────────────────
// Pattern mirror
// ──────────────────────────────────────────────────────────────────────────

const uint8_t* LirMirrorEmitter::emit_pat(const Pattern& p) {
    // B.6 Stage 3.5 step 3: mirror_ptr_ is now field-as-truth. All
    // Pattern construction sites (sema build_pattern_impl, sema
    // make_pat_wild, mono PatSubstWalker) eagerly emit and set
    // mirror_ptr_. The bulk std::visit fallback below should be
    // unreachable; assert and return.
    LOGOS_ASSERT(p.mirror_ptr_ != nullptr,
                 "B6.S35.S3",
                 "emit_pat: Pattern reached without mirror_ptr_ set "
                 "(construction site missed direct-emit migration)");
    if (auto it = table_.pat.find(&p); it != table_.pat.end()) {
        if (it->second == p.mirror_ptr_) return it->second;
        it->second = p.mirror_ptr_;
        return p.mirror_ptr_;
    }
    table_.pat[&p] = p.mirror_ptr_;
    return p.mirror_ptr_;
}


// ──────────────────────────────────────────────────────────────────────────
// Top-level driver
// ──────────────────────────────────────────────────────────────────────────

void LirMirrorEmitter::run(lir::LProgram& prog) {
    // Stage E: functions are now stored as FunctionViews (their decl mirror is
    // emitted eagerly at each push via lir_mirror_emit_fn_view). Nothing to
    // emit here — body sub-nodes were eager-mirrored at sema (Stage D); const
    // value mirrors are eager ExprRefs. run() is retained as the post-sema
    // entry point but has no remaining per-item emit work.
    (void)prog;
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
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, table, prog.type_pool);
    em.run(prog);
    return table;
}

void lir_mirror_struct_append_method(lir::LProgram& prog,
                                     lir_view::StructView sv,
                                     lir_view::FunctionView m) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    em.struct_append_method_direct(sv, m);
}

void lir_mirror_struct_set_methods(lir::LProgram& prog,
                                   lir_view::StructView sv,
                                   const std::vector<lir_view::FunctionView>& ms) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    em.struct_set_methods_direct(sv, ms);
}

void lir_mirror_struct_set_type_code(lir::LProgram& prog,
                                     lir_view::StructView sv,
                                     uint64_t code) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    em.struct_set_type_code_direct(sv, code);
}

// ── DeclBuilder — direct-Writ decl factory ────────────────────────────────
struct DeclBuilder::Impl {
    writ::WritCtr&   ctr;
    LirMirrorTable&      table;
    TypePool&            pool;
    LirMirrorEmitter     em;
    const uint8_t*       map;
    const writ::Arena* arena;
    Impl(writ::WritCtr& c, LirMirrorTable& t, TypePool& p,
         const uint8_t* m, const writ::Arena* a)
        : ctr(c), table(t), pool(p), em(c, t, p), map(m), arena(a) {}
};

DeclBuilder::DeclBuilder(lir::LProgram& prog, lir_schema::decl::Code kind, uint64_t cap) {
    auto& ctr = prog.type_pool.ctr_or_init();
    p_ = std::make_unique<Impl>(ctr, *prog.mirror_table, prog.type_pool,
                                nullptr, prog.type_pool.arena());
    p_->map = p_->em.make_map(writ::schema::lir_stmt(int32_t(kind)), cap);
}
DeclBuilder::DeclBuilder(lir::LProgram& prog, const uint8_t* existing_map) {
    auto& ctr = prog.type_pool.ctr_or_init();
    p_ = std::make_unique<Impl>(ctr, *prog.mirror_table, prog.type_pool,
                                existing_map, prog.type_pool.arena());
}
DeclBuilder::DeclBuilder(std::unique_ptr<Impl> p) noexcept : p_(std::move(p)) {}
DeclBuilder::~DeclBuilder() = default;
DeclBuilder::DeclBuilder(DeclBuilder&&) noexcept = default;
DeclBuilder& DeclBuilder::operator=(DeclBuilder&&) noexcept = default;

void DeclBuilder::str(lir_schema::Key key, std::string_view v) {
    if (!v.empty()) p_->em.put(p_->map, key, p_->em.put_string(v));
}
void DeclBuilder::str_always(lir_schema::Key key, std::string_view v) {
    p_->em.put(p_->map, key, p_->em.put_string(v));
}
void DeclBuilder::i64(lir_schema::Key key, int64_t v) {
    p_->em.put(p_->map, key, p_->em.put_i64(v));
}
void DeclBuilder::i64_if(lir_schema::Key key, int64_t v) {
    if (v) p_->em.put(p_->map, key, p_->em.put_i64(v));
}
void DeclBuilder::flag(lir_schema::Key key, bool v) {
    if (v) p_->em.put(p_->map, key, p_->em.put_bool(true));
}
void DeclBuilder::bool_always(lir_schema::Key key, bool v) {
    p_->em.put(p_->map, key, p_->em.put_bool(v));
}
void DeclBuilder::type(lir_schema::Key key, TypeRef t) {
    if (t) p_->em.put(p_->map, key, p_->em.type_av(t));
}
void DeclBuilder::expr(lir_schema::Key key, lir_view::ExprRef e) {
    if (e) p_->em.put(p_->map, key, p_->em.mref_addr(e.addr()));
}
void DeclBuilder::block(lir_schema::Key key, lir_view::BlockRef b) {
    if (b) p_->em.put(p_->map, key, p_->em.mref_addr(b.addr()));
}
void DeclBuilder::ext_ref(lir_schema::Key key, writ::ExternalRef r) {
    if (r.arena_id().is_valid())
        p_->em.put(p_->map, key, writ::external_ref_av(r));
}
DeclArrayBuilder DeclBuilder::array(lir_schema::Key key) {
    auto arr = p_->em.make_array(0);
    p_->em.put(p_->map, key, p_->em.mref_addr(arr));
    return DeclArrayBuilder(p_.get(), arr);
}
const uint8_t* DeclBuilder::addr() const noexcept { return p_->map; }
const writ::Arena* DeclBuilder::arena() const noexcept { return p_->arena; }

void DeclArrayBuilder::push_str(std::string_view v) {
    owner_->em.array_push(arr_, owner_->em.put_string(v));
}
void DeclArrayBuilder::push_type(TypeRef t) {
    if (t) owner_->em.array_push(arr_, owner_->em.type_av(t));
}
void DeclArrayBuilder::push_ref(const uint8_t* child_addr) {
    owner_->em.array_push(arr_, owner_->em.mref_addr(child_addr));
}
void DeclArrayBuilder::push_tbound(const TraitBound& tb) {
    owner_->em.array_push(arr_, owner_->em.tbound_av(tb));
}
void DeclArrayBuilder::push_param(const lir::LParam& p) {
    owner_->em.array_push(arr_, owner_->em.param_av(p));
}
void DeclArrayBuilder::push_fn_tparam(const TypeParam& tp) {
    owner_->em.array_push(arr_, owner_->em.fn_tparam_av(tp));
}
void DeclArrayBuilder::push_wherebound(const std::pair<TypeRef, std::string>& wb) {
    owner_->em.array_push(arr_, owner_->em.wherebound_av(wb));
}
void DeclArrayBuilder::push_field(const lir::LField& fld) {
    owner_->em.array_push(arr_, owner_->em.field_av(fld));
}
void DeclArrayBuilder::push_annotation(const lir::LAnnotationInstance& ai) {
    owner_->em.array_push(arr_, owner_->em.annot_av(ai));
}
DeclBuilder DeclArrayBuilder::submap(uint64_t schema_code, uint64_t cap) {
    auto sub = owner_->em.make_map(schema_code, cap);
    owner_->em.array_push(arr_, owner_->em.mref_addr(sub));
    auto p = std::make_unique<DeclBuilder::Impl>(owner_->ctr, owner_->table,
                                                 owner_->pool, sub, owner_->arena);
    return DeclBuilder(std::move(p));
}

// ── ObjectMapRef put helpers (Stage E working-state maps) ───────────────────
// Create the ObjectMap in prog's arena on first put, then insert key→value.
static writ::ObjectMap* ensure_obj_map(lir::LProgram& prog, lir_view::ObjectMapRef& ref) {
    auto& arena = prog.type_pool.arena_or_init();
    if (!ref.valid()) {
        auto exp = writ::ObjectMap::create(arena, 8);
        LOGOS_ASSERT(exp.has_value(), "LIR-MIRROR-MAP", "ObjectMap create failed");
        ref.arena_ = &arena;
        ref.addr_  = reinterpret_cast<const uint8_t*>(*exp);   // operator* — Err is move-only
    }
    return ref.map();
}
void lir_mirror_map_put_str(lir::LProgram& prog, lir_view::ObjectMapRef& ref,
                            std::string_view key, std::string_view val) {
    auto& arena = prog.type_pool.arena_or_init();
    auto* m = ensure_obj_map(prog, ref);
    auto exp = writ::ArenaString::create(arena, val);
    LOGOS_ASSERT(exp.has_value(), "LIR-MIRROR-MAP", "value string alloc failed");
    writ::AnyVal av; av.set_ref(reinterpret_cast<const uint8_t*>(*exp));
    auto r = m->put(key, av, arena); LOGOS_ASSERT(r.has_value(), "LIR-MIRROR-MAP", "put failed");
}
void lir_mirror_map_put_ref(lir::LProgram& prog, lir_view::ObjectMapRef& ref,
                            std::string_view key, const uint8_t* val_addr) {
    auto& arena = prog.type_pool.arena_or_init();
    auto* m = ensure_obj_map(prog, ref);
    writ::AnyVal av;
    if (val_addr) av.set_ref(val_addr);
    auto r = m->put(key, av, arena); LOGOS_ASSERT(r.has_value(), "LIR-MIRROR-MAP", "put failed");
}
void lir_mirror_map_put_null(lir::LProgram& prog, lir_view::ObjectMapRef& ref,
                             std::string_view key) {
    auto& arena = prog.type_pool.arena_or_init();
    auto* m = ensure_obj_map(prog, ref);
    auto r = m->put(key, writ::AnyVal{}, arena);   // membership set (null value)
    LOGOS_ASSERT(r.has_value(), "LIR-MIRROR-MAP", "put failed");
}

// macro_arg_blobs: site_id → array of [u64 size][bytes] blobs. Stored as an
// ObjectMap<to_string(site_id)> → ObjectArray of byte-Varchars. The blob bytes
// live as ArenaStrings (separate, stable allocations — array grow never moves
// them), so the shim can hand JIT'd thunk code a raw pointer into them.
void lir_mirror_macro_arg_put(lir::LProgram& prog, lir_view::ObjectMapRef& ref,
                              uint64_t site_id,
                              const std::vector<std::vector<uint8_t>>& blobs) {
    auto& arena = prog.type_pool.arena_or_init();
    auto* m = ensure_obj_map(prog, ref);
    auto arrExp = writ::ObjectArray::create(arena, blobs.empty() ? 1 : blobs.size());
    LOGOS_ASSERT(arrExp.has_value(), "LIR-MIRROR-MAP", "macro-arg array create failed");
    auto* arr = *arrExp;
    for (auto& b : blobs) {
        auto sExp = writ::ArenaString::create(
            arena, std::string_view(reinterpret_cast<const char*>(b.data()), b.size()));
        LOGOS_ASSERT(sExp.has_value(), "LIR-MIRROR-MAP", "macro-arg blob alloc failed");
        writ::AnyVal av; av.set_ref(reinterpret_cast<const uint8_t*>(*sExp));
        auto r = arr->push_back(av, arena); LOGOS_ASSERT(r.has_value(), "LIR-MIRROR-MAP", "macro-arg push failed");
    }
    writ::AnyVal arrAv; arrAv.set_ref(reinterpret_cast<const uint8_t*>(arr));
    auto r = m->put(std::to_string(site_id), arrAv, arena);
    LOGOS_ASSERT(r.has_value(), "LIR-MIRROR-MAP", "macro-arg map put failed");
}

// Shim read: return a pointer to the arg blob's BYTES PAST the 8-byte size
// prefix (matching the old `&blob[8]`), or nullptr if site/arg absent.
const uint8_t* lir_mirror_macro_arg_get(const lir_view::ObjectMapRef& ref,
                                        uint64_t site_id, uint64_t arg_idx) {
    if (!ref.valid()) return nullptr;
    auto site_av = ref.get(std::to_string(site_id));
    if (site_av.is_null()) return nullptr;
    auto* arr = reinterpret_cast<const writ::ObjectArray*>(site_av.resolve());
    if (!arr || arg_idx >= arr->size()) return nullptr;
    auto blob_av = arr->get(arg_idx);
    if (blob_av.is_null()) return nullptr;
    auto* s = reinterpret_cast<const writ::ArenaString*>(blob_av.resolve());
    if (!s || s->view().size() < 8) return nullptr;
    return reinterpret_cast<const uint8_t*>(s->view().data()) + 8;
}

void lir_mirror_emit_into(lir::LProgram& prog, LirMirrorTable& table) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, table, prog.type_pool);
    em.run(prog);
}

const uint8_t* lir_mirror_emit_lit_bool(lir::LProgram& prog, TypeRef ty, bool v) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_lit_bool_direct(ty, v);
}
const uint8_t* lir_mirror_emit_lit_int(lir::LProgram& prog, TypeRef ty, int64_t v) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_lit_int_direct(ty, v);
}
const uint8_t* lir_mirror_emit_lit_int_128(lir::LProgram& prog, TypeRef ty,
                                                   uint64_t lo, uint64_t hi) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_lit_int_direct_128(ty, lo, hi);
}
const uint8_t* lir_mirror_emit_lit_float(lir::LProgram& prog, TypeRef ty, double v) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_lit_float_direct(ty, v);
}
const uint8_t* lir_mirror_emit_lit_str(lir::LProgram& prog, TypeRef ty, std::string_view v) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_lit_str_direct(ty, v);
}
const uint8_t* lir_mirror_emit_var_ref(lir::LProgram& prog, TypeRef ty, std::string_view name,
                                               uint32_t slot) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_var_ref_direct(ty, name, slot);
}
const uint8_t* lir_mirror_emit_addr_of(lir::LProgram& prog, TypeRef ty, std::string_view var_name) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_addr_of_direct(ty, var_name);
}

const uint8_t* lir_mirror_emit_type_alias(lir::LProgram& prog, std::string_view name,
                                          TypeRef type, std::string_view doc) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_type_alias_direct(name, type, doc);
}
const uint8_t* lir_mirror_emit_pack_expand(lir::LProgram& prog, TypeRef ty, std::string_view var_name) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_pack_expand_direct(ty, var_name);
}
const uint8_t* lir_mirror_emit_size_of(lir::LProgram& prog, TypeRef ty, TypeRef elem) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_size_of_direct(ty, elem);
}
const uint8_t* lir_mirror_emit_align_of(lir::LProgram& prog, TypeRef ty, TypeRef elem) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_align_of_direct(ty, elem);
}
const uint8_t* lir_mirror_emit_generic_ref(lir::LProgram& prog, TypeRef ty,
                                                    std::string_view name,
                                                    const std::vector<TypeRef>& type_args) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_generic_ref_direct(ty, name, type_args);
}
const uint8_t* lir_mirror_emit_type_code_of(lir::LProgram& prog, TypeRef ty, TypeRef elem) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_type_code_of_direct(ty, elem);
}
const uint8_t* lir_mirror_emit_reflect_of(lir::LProgram& prog, TypeRef ty, TypeRef elem) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_reflect_of_direct(ty, elem);
}

const uint8_t* lir_mirror_emit_enum_lit(lir::LProgram& prog, TypeRef ty, std::string_view enum_name, std::string_view variant, int64_t disc) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_enum_lit_direct(ty, enum_name, variant, disc);
}
const uint8_t* lir_mirror_emit_enum_lit_data(lir::LProgram& prog, TypeRef ty, std::string_view enum_name, std::string_view variant, int64_t disc, const std::vector<lir_view::ExprRef>& payload) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_enum_lit_data_direct(ty, enum_name, variant, disc, payload);
}
const uint8_t* lir_mirror_emit_struct_lit(lir::LProgram& prog, TypeRef ty, std::string_view name, const std::vector<std::pair<std::string, lir_view::ExprRef>>& fields) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_struct_lit_direct(ty, name, fields);
}
const uint8_t* lir_mirror_emit_call(lir::LProgram& prog, TypeRef ty, std::string_view callee, const std::vector<TypeRef>& type_args, const std::vector<lir_view::ExprRef>& args) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_call_direct(ty, callee, type_args, args);
}
const uint8_t* lir_mirror_emit_method_call(lir::LProgram& prog, TypeRef ty, lir_view::ExprRef receiver, std::string_view method, std::string_view resolved_symbol, const std::vector<TypeRef>& type_args, const std::vector<lir_view::ExprRef>& args, int32_t vtable_index, std::string_view resolved_type, std::string_view tag_system, std::string_view tag_trait) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_method_call_direct(ty, receiver, method, resolved_symbol, type_args, args, vtable_index, resolved_type, tag_system, tag_trait);
}
const uint8_t* lir_mirror_emit_unary(lir::LProgram& prog, TypeRef ty, std::string_view op, lir_view::ExprRef operand) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_unary_direct(ty, op, operand);
}
const uint8_t* lir_mirror_emit_bin_op(lir::LProgram& prog, TypeRef ty, std::string_view op, lir_view::ExprRef lhs, lir_view::ExprRef rhs) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_bin_op_direct(ty, op, lhs, rhs);
}
const uint8_t* lir_mirror_emit_field_read(lir::LProgram& prog, TypeRef ty, lir_view::ExprRef receiver, std::string_view field) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_field_read_direct(ty, receiver, field);
}
const uint8_t* lir_mirror_emit_index_read(lir::LProgram& prog, TypeRef ty, lir_view::ExprRef receiver, lir_view::ExprRef index) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_index_read_direct(ty, receiver, index);
}
const uint8_t* lir_mirror_emit_writ_lit(lir::LProgram& prog, TypeRef ty, const lir::WritValPtr& root, bool has_captures, const std::vector<lir_view::ExprRef>& capture_exprs, const std::vector<TypeRef>& capture_types, uint32_t capture_param_count, std::string_view static_blob) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_writ_lit_direct(ty, root, has_captures, capture_exprs, capture_types, capture_param_count, static_blob);
}
const uint8_t* lir_mirror_emit_deref(lir::LProgram& prog, TypeRef ty, lir_view::ExprRef operand) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_deref_direct(ty, operand);
}
const uint8_t* lir_mirror_emit_cast(lir::LProgram& prog, TypeRef ty, lir_view::ExprRef operand, std::string_view writ_build_fn) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_cast_direct(ty, operand, writ_build_fn);
}
const uint8_t* lir_mirror_emit_try(lir::LProgram& prog, TypeRef ty, lir_view::ExprRef inner, int32_t ok_disc, int32_t err_disc) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_try_direct(ty, inner, ok_disc, err_disc);
}
const uint8_t* lir_mirror_emit_slice_lit(lir::LProgram& prog, TypeRef ty, lir_view::ExprRef base, lir_view::ExprRef len) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_slice_lit_direct(ty, base, len);
}
const uint8_t* lir_mirror_emit_slice_index(lir::LProgram& prog, TypeRef ty, lir_view::ExprRef slice, lir_view::ExprRef index) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_slice_index_direct(ty, slice, index);
}
const uint8_t* lir_mirror_emit_slice_len(lir::LProgram& prog, TypeRef ty, lir_view::ExprRef slice) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_slice_len_direct(ty, slice);
}
const uint8_t* lir_mirror_emit_slice_ptr(lir::LProgram& prog, TypeRef ty, lir_view::ExprRef slice) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_slice_ptr_direct(ty, slice);
}
const uint8_t* lir_mirror_emit_addr_of_temp(lir::LProgram& prog, TypeRef ty, lir_view::ExprRef inner, bool is_mut) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_addr_of_temp_direct(ty, inner, is_mut);
}
const uint8_t* lir_mirror_emit_ptr_arith(lir::LProgram& prog, TypeRef ty, uint8_t op, lir_view::ExprRef ptr, lir_view::ExprRef offset) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_ptr_arith_direct(ty, op, ptr, offset);
}
const uint8_t* lir_mirror_emit_ptr_diff(lir::LProgram& prog, TypeRef ty, bool by_byte, lir_view::ExprRef lhs, lir_view::ExprRef rhs) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_ptr_diff_direct(ty, by_byte, lhs, rhs);
}
const uint8_t* lir_mirror_emit_if_expr(lir::LProgram& prog, TypeRef ty, lir_view::ExprRef cond, lir_view::ExprRef then_val, lir_view::ExprRef else_val) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_if_expr_direct(ty, cond, then_val, else_val);
}
const uint8_t* lir_mirror_emit_tuple_lit(lir::LProgram& prog, TypeRef ty, const std::vector<lir_view::ExprRef>& elems) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_tuple_lit_direct(ty, elems);
}
const uint8_t* lir_mirror_emit_tuple_index(lir::LProgram& prog, TypeRef ty, lir_view::ExprRef receiver, uint32_t index) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_tuple_index_direct(ty, receiver, index);
}
const uint8_t* lir_mirror_emit_arr_lit(lir::LProgram& prog, TypeRef ty, const std::vector<lir_view::ExprRef>& elems) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_arr_lit_direct(ty, elems);
}
const uint8_t* lir_mirror_emit_block_expr(lir::LProgram& prog, TypeRef ty, lir_view::BlockRef block, lir_view::ExprRef result) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_block_expr_direct(ty, block, result);
}
const uint8_t* lir_mirror_emit_closure_call(lir::LProgram& prog, TypeRef ty, lir_view::ExprRef callee, const std::vector<lir_view::ExprRef>& args) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_closure_call_direct(ty, callee, args);
}
const uint8_t* lir_mirror_emit_fn_ptr_call(lir::LProgram& prog, TypeRef ty, lir_view::ExprRef callee, const std::vector<lir_view::ExprRef>& args) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_fn_ptr_call_direct(ty, callee, args);
}
const uint8_t* lir_mirror_emit_match_expr(lir::LProgram& prog, TypeRef ty, lir_view::ExprRef scrut, const std::vector<lir::EMatchArm>& arms) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_match_expr_direct(ty, scrut, arms);
}
const uint8_t* lir_mirror_emit_match_expr(lir::LProgram& prog, TypeRef ty, lir_view::ExprRef scrut, const std::vector<lir::EMatchArmView>& arms) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_match_expr_direct(ty, scrut, arms);
}
const uint8_t* lir_mirror_emit_format_call(lir::LProgram& prog, TypeRef ty, lir_view::ExprRef fmt, const std::vector<lir_view::ExprRef>& args, const std::vector<TypeRef>& arg_types) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_format_call_direct(ty, fmt, args, arg_types);
}
const uint8_t* lir_mirror_emit_closure_box(lir::LProgram& prog, TypeRef ty, const lir::EClosure* inner) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_closure_box_direct(ty, inner);
}

// ── Stage B.6 — LStmt direct mirror writers ──────────────────────────────
const uint8_t* lir_mirror_emit_let(lir::LProgram& prog, uint32_t line, std::string_view name, TypeRef ty, lir_view::ExprRef value, bool is_mut, uint32_t slot, bool compiler_glue) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_let_direct(line, name, ty, value, is_mut, slot, compiler_glue);
}
const uint8_t* lir_mirror_emit_assign(lir::LProgram& prog, uint32_t line, std::string_view name, lir_view::ExprRef value, bool drop_old) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_assign_direct(line, name, value, drop_old);
}
const uint8_t* lir_mirror_emit_return(lir::LProgram& prog, uint32_t line, lir_view::ExprRef value) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_return_direct(line, value);
}
const uint8_t* lir_mirror_emit_if_stmt(lir::LProgram& prog, uint32_t line, lir_view::ExprRef cond, lir_view::BlockRef then_blk, lir_view::BlockRef else_blk) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_if_stmt_direct(line, cond, then_blk, else_blk);
}
const uint8_t* lir_mirror_emit_while(lir::LProgram& prog, uint32_t line, lir_view::ExprRef cond, lir_view::BlockRef body, std::string_view label) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_while_direct(line, cond, body, label);
}
const uint8_t* lir_mirror_emit_for(lir::LProgram& prog, uint32_t line, std::string_view var, lir_view::ExprRef lo, lir_view::ExprRef hi, bool inclusive, lir_view::BlockRef body, std::string_view label, uint32_t slot) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_for_direct(line, var, lo, hi, inclusive, body, label, slot);
}
const uint8_t* lir_mirror_emit_loop(lir::LProgram& prog, uint32_t line, lir_view::BlockRef body, std::string_view label, std::string_view break_slot, TypeRef result_type) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_loop_direct(line, body, label, break_slot, result_type);
}
const uint8_t* lir_mirror_emit_break(lir::LProgram& prog, uint32_t line, lir_view::ExprRef value, std::string_view label) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_break_direct(line, value, label);
}
const uint8_t* lir_mirror_emit_continue(lir::LProgram& prog, uint32_t line, std::string_view label) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_continue_direct(line, label);
}
const uint8_t* lir_mirror_emit_block_stmt(lir::LProgram& prog, uint32_t line, lir_view::BlockRef body, bool transparent) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_block_stmt_direct(line, body, transparent);
}
const uint8_t* lir_mirror_emit_field_write(lir::LProgram& prog, uint32_t line, std::string_view receiver, std::string_view field, lir_view::ExprRef value) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_field_write_direct(line, receiver, field, value);
}
const uint8_t* lir_mirror_emit_index_write(lir::LProgram& prog, uint32_t line, std::string_view arr, lir_view::ExprRef index, lir_view::ExprRef value) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_index_write_direct(line, arr, index, value);
}
const uint8_t* lir_mirror_emit_field_index_write(lir::LProgram& prog, uint32_t line, std::string_view receiver, std::string_view field, lir_view::ExprRef index, lir_view::ExprRef value) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_field_index_write_direct(line, receiver, field, index, value);
}
const uint8_t* lir_mirror_emit_expr_stmt(lir::LProgram& prog, uint32_t line, lir_view::ExprRef expr) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_expr_stmt_direct(line, expr);
}
const uint8_t* lir_mirror_emit_match_stmt(lir::LProgram& prog, uint32_t line, lir_view::ExprRef scrut, const std::vector<lir::LMatchArm>& arms) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_match_stmt_direct(line, scrut, arms);
}
const uint8_t* lir_mirror_emit_for_each(lir::LProgram& prog, uint32_t line, std::string_view var, lir_view::ExprRef iter, TypeRef elem_type, int64_t arr_size, bool is_slice, lir_view::BlockRef body, uint32_t slot, bool var_mut) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_for_each_direct(line, var, iter, elem_type, arr_size, is_slice, body, slot, var_mut);
}
const uint8_t* lir_mirror_emit_deref_write(lir::LProgram& prog, uint32_t line, lir_view::ExprRef ptr, lir_view::ExprRef value, bool drop_old) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_deref_write_direct(line, ptr, value, drop_old);
}
const uint8_t* lir_mirror_emit_drop(lir::LProgram& prog, uint32_t line, std::string_view var_name, std::string_view drop_fn, TypeRef ty, bool drop_fields, const std::vector<std::string>& moved_fields) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_drop_direct(line, var_name, drop_fn, ty, drop_fields, moved_fields);
}
const uint8_t* lir_mirror_emit_deref_field_write(lir::LProgram& prog, uint32_t line, std::string_view receiver, std::string_view type_name, std::string_view field, lir_view::ExprRef value) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_deref_field_write_direct(line, receiver, type_name, field, value);
}
const uint8_t* lir_mirror_emit_tuple_write(lir::LProgram& prog, uint32_t line, std::string_view receiver, uint32_t index, lir_view::ExprRef value, TypeRef recv_type) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_tuple_write_direct(line, receiver, index, value, recv_type);
}
const uint8_t* lir_mirror_emit_let_else(lir::LProgram& prog, uint32_t line, const lir::Pattern& pat, lir_view::ExprRef scrut, lir_view::BlockRef else_block, const std::vector<lir::LExprPtr>& guards) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_let_else_direct(line, pat, scrut, else_block, guards);
}
const uint8_t* lir_mirror_emit_chain_field_write(lir::LProgram& prog, uint32_t line, std::string_view receiver, std::string_view mid_field, const std::vector<std::string>& extras, std::string_view field, lir_view::ExprRef value) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_chain_field_write_direct(line, receiver, mid_field, extras, field, value);
}

// ── Stage B.6 — WritVal direct mirror writers ──────────────────────────
const uint8_t* lir_mirror_emit_hv_null(lir::LProgram& prog) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_hv_null_direct();
}
const uint8_t* lir_mirror_emit_hv_bool(lir::LProgram& prog, bool value) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_hv_bool_direct(value);
}
const uint8_t* lir_mirror_emit_hv_int(lir::LProgram& prog, int64_t value) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_hv_int_direct(value);
}
const uint8_t* lir_mirror_emit_hv_float(lir::LProgram& prog, double value) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_hv_float_direct(value);
}
const uint8_t* lir_mirror_emit_hv_str(lir::LProgram& prog, std::string_view value) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_hv_str_direct(value);
}
const uint8_t* lir_mirror_emit_hv_map(lir::LProgram& prog, const std::vector<lir::WVMapEntry>& entries, std::string_view key_type) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_hv_map_direct(entries, key_type);
}
const uint8_t* lir_mirror_emit_hv_array(lir::LProgram& prog, const std::vector<lir::WritValPtr>& elements, std::string_view elem_type) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_hv_array_direct(elements, elem_type);
}
const uint8_t* lir_mirror_emit_hv_capture(lir::LProgram& prog, uint32_t param_index, uint32_t value_index) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_hv_capture_direct(param_index, value_index);
}
const uint8_t* lir_mirror_emit_hv_type(lir::LProgram& prog, uint32_t kind, uint64_t uid, std::string_view name) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_hv_type_direct(kind, uid, name);
}

// ── Stage B.6 — Pattern direct mirror writers ────────────────────────────
const uint8_t* lir_mirror_emit_pat_variant(lir::LProgram& prog, std::string_view enum_name, std::string_view variant, int64_t disc) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_variant_direct(enum_name, variant, disc);
}
const uint8_t* lir_mirror_emit_pat_int(lir::LProgram& prog, int64_t value) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_int_direct(value);
}
const uint8_t* lir_mirror_emit_pat_bool(lir::LProgram& prog, bool value) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_bool_direct(value);
}
const uint8_t* lir_mirror_emit_pat_wild(lir::LProgram& prog, std::string_view name, uint32_t slot, bool is_mut) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_wild_direct(name, slot, is_mut);
}
const uint8_t* lir_mirror_emit_pat_variant_data(lir::LProgram& prog, std::string_view enum_name, std::string_view variant, int64_t disc, const std::vector<std::string>& bindings, const std::vector<TypeRef>& binding_types, const std::vector<uint32_t>& bind_slots, const std::vector<uint32_t>& bind_ref_modes) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_variant_data_direct(enum_name, variant, disc, bindings, binding_types, bind_slots, bind_ref_modes);
}
const uint8_t* lir_mirror_emit_pat_or(lir::LProgram& prog, const std::vector<lir::Pattern>& alts) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_or_direct(alts);
}
const uint8_t* lir_mirror_emit_pat_tuple(lir::LProgram& prog, const std::vector<std::string>& bindings, const std::vector<TypeRef>& binding_types, const std::vector<lir::Pattern>& subs, const std::vector<uint32_t>& bind_slots) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_tuple_direct(bindings, binding_types, subs, bind_slots);
}
const uint8_t* lir_mirror_emit_pat_range(lir::LProgram& prog, __int128 lo, __int128 hi) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_range_direct(lo, hi);
}
const uint8_t* lir_mirror_emit_pat_struct(lir::LProgram& prog, std::string_view struct_name, const std::vector<lir::PatFieldBinding>& fields, bool has_rest) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_struct_direct(struct_name, fields, has_rest);
}
const uint8_t* lir_mirror_emit_pat_slice(lir::LProgram& prog, const std::vector<lir::Pattern>& prefix, const std::vector<lir::Pattern>& rest, const std::vector<lir::Pattern>& suffix) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_slice_direct(prefix, rest, suffix);
}
const uint8_t* lir_mirror_emit_pat_at(lir::LProgram& prog, std::string_view name, const std::vector<lir::Pattern>& sub, TypeRef type, uint32_t slot) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_at_direct(name, sub, type, slot);
}
const uint8_t* lir_mirror_emit_pat_ref_bind(lir::LProgram& prog, std::string_view name, bool is_mut, TypeRef bind_type, uint32_t slot) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_ref_bind_direct(name, is_mut, bind_type, slot);
}
const uint8_t* lir_mirror_emit_pat_ref_pat(lir::LProgram& prog, const std::vector<lir::Pattern>& inner, bool is_mut) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_ref_pat_direct(inner, is_mut);
}

void lir_mirror_retype_expr(lir::LProgram& prog,
                            const uint8_t* expr_addr,
                            TypeRef new_ty) {
    if (expr_addr == nullptr) return;
    auto& ctr = prog.type_pool.ctr_or_init();
    auto tom = writ::TinyMapView(
        reinterpret_cast<writ::TinyObjectMap*>(const_cast<uint8_t*>(expr_addr)),
        ctr.holder());
    writ::AnyVal av;
    if (new_ty) av.set_ref(new_ty.addr());
    if (av.is_null()) return;
    auto r = tom.put(lir_schema::expr_common::TYPE.code, av);
    LOGOS_ASSERT(r.has_value(), "LIR-MIRROR-006",
        "retype_expr put failed");
}

void lir_mirror_populate_moved(lir::LProgram& prog, LirMirrorTable& table) {
    // Stage E: impl methods are FunctionViews (decl mirror eager-emitted at
    // push); their bridge IS the mirror pointer, so a std::move carries them
    // intact — nothing to back-fill. const value mirrors are eager ExprRefs.
    (void)prog; (void)table;
}

// ── Per-node entry points (Stage 3g.1) ────────────────────────────────────
//
// LirBuilder calls these immediately after constructing each variant. The
// emitter's per-node emit_* functions are memoized via the table, so a node
// emitted here is a cache hit when later walked by lir_mirror_emit_into —
// which keeps existing post-sema and per-clone passes correct without
// modification.

// EAGER block completion primitive: emit a block container from its collected
// statement refs and return a BlockRef handle. Call at each block's TRUE
// completion point (after the LAST push into the block).
lir_view::BlockRef lir_mirror_block(lir::LProgram& prog,
                                    const std::vector<lir_view::StmtRef>& stmts) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return lir_view::BlockRef(prog.type_pool.arena(), em.emit_block_stmts_public(stmts));
}
const uint8_t* lir_mirror_emit_pat_node(lir::LProgram& prog, const lir::Pattern& p) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_pat_public(p);
}
const uint8_t* lir_mirror_emit_hv_node(lir::LProgram& prog, const lir::WritVal& v) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_hv_public(v);
}

} // namespace logos::compiler
