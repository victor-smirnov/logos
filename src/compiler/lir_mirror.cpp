// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
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
    // Dry-run mode: when true, make_map / make_array / put / array_push are
    // no-ops. Used to back-fill the table's reverse-lookup maps for nodes that
    // were already mirrored under a different table (e.g. carried over via
    // std::move(in_.consts)). The variant walk still recurses through child
    // emit_* calls so descendants back-fill themselves.
    bool dry_run_ = false;

public:
    LirMirrorEmitter(hermes::Arena& a, LirMirrorTable& t) : arena_(a), table_(t) {}

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

    // EStructLit / ENew fields: emit FIELD_NAMES + FIELD_VALUES parallel arrays
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

    auto map_off = make_map(hermes::schema::lir_expr(lir_schema::expr::Code::ClosureBox)
                            | (1ULL << 47));  // closure marker bit (synthetic)
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
    return map_off;
}

// ──────────────────────────────────────────────────────────────────────────
// HermesVal mirror
// ──────────────────────────────────────────────────────────────────────────

hermes::arena_offset_t LirMirrorEmitter::emit_hv(const HermesVal& v) {
    bool backfill_only = false;
    if (v.mirror_offset_ != hermes::arena_offset_t{}) {
        if (auto it = table_.hermes_val.find(&v); it != table_.hermes_val.end()) {
            return it->second;
        }
        table_.hermes_val[&v] = v.mirror_offset_;
        backfill_only = true;
    } else if (auto it = table_.hermes_val.find(&v); it != table_.hermes_val.end()) {
        // Heap-address recycling: stale cache entry for a freed HermesVal.
        // Invalidate and fall through to emit a fresh mirror.
        table_.hermes_val.erase(it);
    }

    bool save_dry = dry_run_;
    if (backfill_only) dry_run_ = true;

    using namespace lir;
    int32_t code = v.kind.index();
    // HermesVal lives outside the formal expr/stmt/pat enum — give it its own
    // synthetic category by piggybacking on lir_expr with an offset into the
    // unused code range. (Phase 3a left no dedicated category for HV; OK for
    // now since no readers consume mirrors yet.)
    constexpr int32_t HV_BASE = 200;

    hermes::arena_offset_t map_off;
    std::visit([&](auto const& alt) {
        using T = std::decay_t<decltype(alt)>;
        if constexpr (std::is_same_v<T, HVNull>) {
            map_off = make_map(hermes::schema::lir_expr(HV_BASE + 0));
        } else if constexpr (std::is_same_v<T, HVBool>) {
            map_off = make_map(hermes::schema::lir_expr(HV_BASE + 1));
            put(map_off, hk::BOOL_VALUE, put_bool(alt.value));
        } else if constexpr (std::is_same_v<T, HVInt>) {
            map_off = make_map(hermes::schema::lir_expr(HV_BASE + 2));
            put(map_off, hk::INT_VALUE, put_i64(alt.value));
        } else if constexpr (std::is_same_v<T, HVFloat>) {
            map_off = make_map(hermes::schema::lir_expr(HV_BASE + 3));
            put(map_off, hk::FLOAT_VALUE, put_f64(alt.value));
        } else if constexpr (std::is_same_v<T, HVStr>) {
            map_off = make_map(hermes::schema::lir_expr(HV_BASE + 4));
            put(map_off, hk::STR_VALUE, put_string(alt.value));
        } else if constexpr (std::is_same_v<T, HVMap>) {
            // Pre-emit values
            std::vector<hermes::AnyVal> key_strs, key_ints, val_avs;
            key_strs.reserve(alt.entries.size());
            key_ints.reserve(alt.entries.size());
            val_avs.reserve(alt.entries.size());
            for (auto& e : alt.entries) {
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
            map_off = make_map(hermes::schema::lir_expr(HV_BASE + 5));
            put(map_off, hk::MAP_KEYS,   keys_av);
            put(map_off, hk::MAP_VALUES, vals_av);
            if (!alt.key_type.empty())
                put(map_off, hk::TYPE_NAME, put_string(alt.key_type));
        } else if constexpr (std::is_same_v<T, HVArray>) {
            std::vector<hermes::AnyVal> elems;
            elems.reserve(alt.elements.size());
            for (auto& e : alt.elements) elems.push_back(hv_av(e));
            hermes::AnyVal arr_av;
            if (!elems.empty()) {
                auto off = make_array(elems.size());
                for (auto av : elems) array_push(off, av);
                arr_av = hermes::AnyVal::from_offset(off);
            }
            map_off = make_map(hermes::schema::lir_expr(HV_BASE + 6));
            put(map_off, hk::ELEMS, arr_av);
            if (!alt.elem_type.empty())
                put(map_off, hk::TYPE_NAME, put_string(alt.elem_type));
        } else if constexpr (std::is_same_v<T, HVCapture>) {
            map_off = make_map(hermes::schema::lir_expr(HV_BASE + 7));
            put(map_off, hk::PARAM_INDEX, put_u32(alt.param_index));
            put(map_off, hk::VALUE_INDEX, put_u32(alt.value_index));
        }
    }, v.kind);
    (void)code;
    dry_run_ = save_dry;
    if (backfill_only) return v.mirror_offset_;
    v.mirror_offset_ = map_off;
    table_.hermes_val[&v] = map_off;
    return map_off;
}

// ──────────────────────────────────────────────────────────────────────────
// Pattern mirror
// ──────────────────────────────────────────────────────────────────────────

hermes::arena_offset_t LirMirrorEmitter::emit_pat(const Pattern& p) {
    if (auto it = table_.pat.find(&p); it != table_.pat.end())
        return it->second;
    using namespace lir;
    hermes::arena_offset_t map_off;
    std::visit([&](auto const& alt) {
        using T = std::decay_t<decltype(alt)>;
        if constexpr (std::is_same_v<T, PatVariant>) {
            auto enum_av    = put_string(alt.enum_name);
            auto variant_av = put_string(alt.variant);
            map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::Variant));
            put(map_off, pk::ENUM_NAME, enum_av);
            put(map_off, pk::VARIANT,   variant_av);
            put(map_off, pk::DISC,      put_i64(alt.disc));
        } else if constexpr (std::is_same_v<T, PatInt>) {
            map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::Int));
            put(map_off, pk::INT_VALUE, put_i64(alt.value));
        } else if constexpr (std::is_same_v<T, PatBool>) {
            map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::Bool));
            put(map_off, pk::BOOL_VALUE, put_bool(alt.value));
        } else if constexpr (std::is_same_v<T, PatWild>) {
            auto name_av = alt.name.empty() ? hermes::AnyVal{} : put_string(alt.name);
            map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::Wild));
            put(map_off, pk::NAME, name_av);
        } else if constexpr (std::is_same_v<T, PatVariantData>) {
            auto enum_av    = put_string(alt.enum_name);
            auto variant_av = put_string(alt.variant);
            auto bindings_av = string_array(alt.bindings);
            auto btypes_av   = type_array(alt.binding_types);
            map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::VariantData));
            put(map_off, pk::ENUM_NAME,      enum_av);
            put(map_off, pk::VARIANT,        variant_av);
            put(map_off, pk::DISC,           put_i64(alt.disc));
            put(map_off, pk::BINDINGS,       bindings_av);
            put(map_off, pk::BINDING_TYPES,  btypes_av);
        } else if constexpr (std::is_same_v<T, PatOr>) {
            auto subs_av = pat_array(alt.alts);
            map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::Or));
            put(map_off, pk::SUBS, subs_av);
        } else if constexpr (std::is_same_v<T, PatTuple>) {
            auto bindings_av = string_array(alt.bindings);
            auto btypes_av   = type_array(alt.binding_types);
            auto subs_av     = pat_array(alt.subs);
            map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::Tuple));
            put(map_off, pk::BINDINGS,      bindings_av);
            put(map_off, pk::BINDING_TYPES, btypes_av);
            put(map_off, pk::SUBS,          subs_av);
        } else if constexpr (std::is_same_v<T, PatRange>) {
            map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::Range));
            put(map_off, pk::LO, put_i64(alt.lo));
            put(map_off, pk::HI, put_i64(alt.hi));
        } else if constexpr (std::is_same_v<T, PatStruct>) {
            auto name_av   = put_string(alt.struct_name);
            auto fields_av = field_binding_array(alt.fields);
            map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::Struct));
            put(map_off, pk::STRUCT_NAME, name_av);
            put(map_off, pk::FIELDS,      fields_av);
            put(map_off, pk::HAS_REST,    put_bool(alt.has_rest));
        } else if constexpr (std::is_same_v<T, PatSlice>) {
            auto pre_av  = pat_array(alt.prefix);
            auto rest_av = pat_array(alt.rest);
            auto suf_av  = pat_array(alt.suffix);
            map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::Slice));
            put(map_off, pk::PREFIX, pre_av);
            put(map_off, pk::REST,   rest_av);
            put(map_off, pk::SUFFIX, suf_av);
        } else if constexpr (std::is_same_v<T, PatAt>) {
            auto name_av = put_string(alt.name);
            auto sub_av  = pat_array(alt.sub);
            map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::At));
            put(map_off, pk::NAME, name_av);
            put(map_off, pk::SUB,  sub_av);
            put(map_off, pk::TYPE, type_av(alt.type));
        } else if constexpr (std::is_same_v<T, PatRefBind>) {
            auto name_av = put_string(alt.name);
            map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::RefBind));
            put(map_off, pk::NAME,      name_av);
            put(map_off, pk::IS_MUT,    put_bool(alt.is_mut));
            put(map_off, pk::BIND_TYPE, type_av(alt.bind_type));
        } else if constexpr (std::is_same_v<T, PatRefPat>) {
            auto inner_av = pat_array(alt.inner);
            map_off = make_map(hermes::schema::lir_pat(lir_schema::pat::Code::RefPat));
            put(map_off, pk::INNER,  inner_av);
            put(map_off, pk::IS_MUT, put_bool(alt.is_mut));
        }
    }, p);
    table_.pat[&p] = map_off;
    return map_off;
}

// ──────────────────────────────────────────────────────────────────────────
// LStmt mirror
// ──────────────────────────────────────────────────────────────────────────

hermes::arena_offset_t LirMirrorEmitter::emit_stmt(const LStmt& s) {
    bool backfill_only = false;
    if (s.mirror_offset_ != hermes::arena_offset_t{}) {
        if (auto it = table_.stmt.find(&s); it != table_.stmt.end()) {
            return it->second;
        }
        table_.stmt[&s] = s.mirror_offset_;
        table_.stmt_by_offset[s.mirror_offset_.value()] = &s;
        backfill_only = true;
    } else if (auto it = table_.stmt.find(&s); it != table_.stmt.end()) {
        // Heap-address recycling: stale cache entry for a freed LStmt.
        // Invalidate and fall through to emit a fresh mirror.
        table_.stmt_by_offset.erase(it->second.value());
        table_.stmt.erase(it);
    }

    bool save_dry = dry_run_;
    if (backfill_only) dry_run_ = true;

    using namespace lir;
    hermes::arena_offset_t map_off;
    std::visit([&](auto const& alt) {
        using T = std::decay_t<decltype(alt)>;
        if constexpr (std::is_same_v<T, SLet>) {
            auto name_av = put_string(alt.name);
            auto val_av  = expr_av(alt.value);
            map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::Let));
            put(map_off, sk::NAME,   name_av);
            put(map_off, sk::TYPE,   type_av(alt.type));
            put(map_off, sk::VALUE,  val_av);
            put(map_off, sk::IS_MUT, put_bool(alt.is_mut));
        } else if constexpr (std::is_same_v<T, SAssign>) {
            auto name_av = put_string(alt.name);
            auto val_av  = expr_av(alt.value);
            map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::Assign));
            put(map_off, sk::NAME,  name_av);
            put(map_off, sk::VALUE, val_av);
        } else if constexpr (std::is_same_v<T, SReturn>) {
            auto val_av = expr_av(alt.value);
            map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::Return));
            put(map_off, sk::VALUE, val_av);
        } else if constexpr (std::is_same_v<T, SIf>) {
            auto cond_av = expr_av(alt.cond);
            auto then_av = block_av(alt.then_);
            hermes::AnyVal else_av;
            if (alt.else_.has_value()) else_av = block_av(*alt.else_);
            map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::If));
            put(map_off, sk::COND,       cond_av);
            put(map_off, sk::THEN_BLOCK, then_av);
            put(map_off, sk::ELSE_BLOCK, else_av);
        } else if constexpr (std::is_same_v<T, SWhile>) {
            auto cond_av = expr_av(alt.cond);
            auto body_av = block_av(alt.body);
            hermes::AnyVal label_av;
            if (!alt.label.empty()) label_av = put_string(alt.label);
            map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::While));
            put(map_off, sk::COND,  cond_av);
            put(map_off, sk::BODY,  body_av);
            put(map_off, sk::LABEL, label_av);
        } else if constexpr (std::is_same_v<T, SFor>) {
            auto var_av  = put_string(alt.var);
            auto lo_av   = expr_av(alt.lo);
            auto hi_av   = expr_av(alt.hi);
            auto body_av = block_av(alt.body);
            hermes::AnyVal label_av;
            if (!alt.label.empty()) label_av = put_string(alt.label);
            map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::For));
            put(map_off, sk::VAR,       var_av);
            put(map_off, sk::LO,        lo_av);
            put(map_off, sk::HI,        hi_av);
            put(map_off, sk::INCLUSIVE, put_bool(alt.inclusive));
            put(map_off, sk::BODY,      body_av);
            put(map_off, sk::LABEL,     label_av);
        } else if constexpr (std::is_same_v<T, SLoop>) {
            auto body_av = block_av(alt.body);
            hermes::AnyVal label_av, slot_av;
            if (!alt.label.empty())      label_av = put_string(alt.label);
            if (!alt.break_slot.empty()) slot_av  = put_string(alt.break_slot);
            map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::Loop));
            put(map_off, sk::BODY,        body_av);
            put(map_off, sk::LABEL,       label_av);
            put(map_off, sk::BREAK_SLOT,  slot_av);
            put(map_off, sk::RESULT_TYPE, type_av(alt.result_type));
        } else if constexpr (std::is_same_v<T, SBreak>) {
            auto val_av = expr_av(alt.value);
            hermes::AnyVal label_av;
            if (!alt.label.empty()) label_av = put_string(alt.label);
            map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::Break));
            put(map_off, sk::VALUE, val_av);
            put(map_off, sk::LABEL, label_av);
        } else if constexpr (std::is_same_v<T, SContinue>) {
            hermes::AnyVal label_av;
            if (!alt.label.empty()) label_av = put_string(alt.label);
            map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::Continue));
            put(map_off, sk::LABEL, label_av);
        } else if constexpr (std::is_same_v<T, SBlock>) {
            auto body_av = block_av(alt.body);
            map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::Block));
            put(map_off, sk::BODY, body_av);
        } else if constexpr (std::is_same_v<T, SFieldWrite>) {
            auto recv_av  = put_string(alt.receiver);
            auto field_av = put_string(alt.field);
            auto val_av   = expr_av(alt.value);
            map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::FieldWrite));
            put(map_off, sk::RECEIVER, recv_av);
            put(map_off, sk::FIELD,    field_av);
            put(map_off, sk::VALUE,    val_av);
        } else if constexpr (std::is_same_v<T, SIndexWrite>) {
            auto arr_av  = put_string(alt.arr);
            auto idx_av  = expr_av(alt.index);
            auto val_av  = expr_av(alt.value);
            map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::IndexWrite));
            put(map_off, sk::NAME,  arr_av);
            put(map_off, sk::INDEX, idx_av);
            put(map_off, sk::VALUE, val_av);
        } else if constexpr (std::is_same_v<T, SFieldIndexWrite>) {
            auto recv_av  = put_string(alt.receiver);
            auto field_av = put_string(alt.field);
            auto idx_av   = expr_av(alt.index);
            auto val_av   = expr_av(alt.value);
            map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::FieldIndexWrite));
            put(map_off, sk::RECEIVER, recv_av);
            put(map_off, sk::FIELD,    field_av);
            put(map_off, sk::INDEX,    idx_av);
            put(map_off, sk::VALUE,    val_av);
        } else if constexpr (std::is_same_v<T, SExprStmt>) {
            auto expr_avv = expr_av(alt.expr);
            map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::ExprStmt));
            put(map_off, sk::EXPR, expr_avv);
        } else if constexpr (std::is_same_v<T, SMatch>) {
            auto scrut_av = expr_av(alt.scrut);
            auto arms_av  = arm_array(alt.arms);
            map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::Match));
            put(map_off, sk::SCRUT, scrut_av);
            put(map_off, sk::ARMS,  arms_av);
        } else if constexpr (std::is_same_v<T, SDelete>) {
            auto expr_avv = expr_av(alt.expr);
            map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::Delete));
            put(map_off, sk::EXPR, expr_avv);
        } else if constexpr (std::is_same_v<T, SForEach>) {
            auto var_av  = put_string(alt.var);
            auto iter_av = expr_av(alt.iter);
            auto body_av = block_av(alt.body);
            map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::ForEach));
            put(map_off, sk::VAR,       var_av);
            put(map_off, sk::ITER,      iter_av);
            put(map_off, sk::ELEM_TYPE, type_av(alt.elem_type));
            put(map_off, sk::ARR_SIZE,  put_i64(alt.arr_size));
            put(map_off, sk::IS_SLICE,  put_bool(alt.is_slice));
            put(map_off, sk::BODY,      body_av);
        } else if constexpr (std::is_same_v<T, SDerefWrite>) {
            auto ptr_av = expr_av(alt.ptr);
            auto val_av = expr_av(alt.value);
            map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::DerefWrite));
            put(map_off, sk::PTR,   ptr_av);
            put(map_off, sk::VALUE, val_av);
        } else if constexpr (std::is_same_v<T, SDrop>) {
            auto var_av = put_string(alt.var_name);
            hermes::AnyVal drop_av;
            if (!alt.drop_fn.empty()) drop_av = put_string(alt.drop_fn);
            map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::Drop));
            put(map_off, sk::NAME,         var_av);
            put(map_off, sk::DROP_FN,      drop_av);
            put(map_off, sk::TYPE,         type_av(alt.type));
            put(map_off, sk::DROP_FIELDS,  put_bool(alt.drop_fields));
        } else if constexpr (std::is_same_v<T, SDerefFieldWrite>) {
            auto recv_av  = put_string(alt.receiver);
            auto type_avv = put_string(alt.type_name);
            auto field_av = put_string(alt.field);
            auto val_av   = expr_av(alt.value);
            map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::DerefFieldWrite));
            put(map_off, sk::RECEIVER,   recv_av);
            put(map_off, sk::TYPE_NAME,  type_avv);
            put(map_off, sk::FIELD,      field_av);
            put(map_off, sk::VALUE,      val_av);
        } else if constexpr (std::is_same_v<T, STupleWrite>) {
            auto recv_av = put_string(alt.receiver);
            auto val_av  = expr_av(alt.value);
            map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::TupleWrite));
            put(map_off, sk::RECEIVER,        recv_av);
            put(map_off, sk::TUPLE_INDEX_VAL, put_u32(alt.index));
            put(map_off, sk::VALUE,           val_av);
            put(map_off, sk::RECV_TYPE,       type_av(alt.recv_type));
        } else if constexpr (std::is_same_v<T, SLetElse>) {
            auto pat_off  = emit_pat(alt.pat);
            auto scrut_av = expr_av(alt.scrut);
            auto eb_av    = block_av(alt.else_block);
            map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::LetElse));
            put(map_off, sk::PAT,           hermes::AnyVal::from_offset(pat_off));
            put(map_off, sk::SCRUT,         scrut_av);
            put(map_off, sk::ELSE_DIVERGE,  eb_av);
        } else if constexpr (std::is_same_v<T, SChainFieldWrite>) {
            auto recv_av = put_string(alt.receiver);
            auto mid_av  = put_string(alt.mid_field);
            auto fld_av  = put_string(alt.field);
            auto val_av  = expr_av(alt.value);
            map_off = make_map(hermes::schema::lir_stmt(lir_schema::stmt::Code::ChainFieldWrite));
            put(map_off, sk::RECEIVER,  recv_av);
            put(map_off, sk::MID_FIELD, mid_av);
            put(map_off, sk::FIELD,     fld_av);
            put(map_off, sk::VALUE,     val_av);
        }
    }, s.kind);

    if (s.line != 0)
        put(map_off, sc::LINE, put_u32(s.line));

    dry_run_ = save_dry;
    if (backfill_only) return s.mirror_offset_;

    s.mirror_offset_ = map_off;
    table_.stmt[&s] = map_off;
    table_.stmt_by_offset[map_off.value()] = &s;
    return map_off;
}

// ──────────────────────────────────────────────────────────────────────────
// LExpr mirror — biggest switch
// ──────────────────────────────────────────────────────────────────────────

hermes::arena_offset_t LirMirrorEmitter::emit_expr(const LExpr& e) {
    // Field-as-truth fast path. The field is the cross-table back-pointer
    // (set on first emission); honouring it before the cache prevents stale
    // cache hits from reused heap addresses (LExpr deletion + std::make_unique
    // recycling). When the field is set but the current table doesn't know
    // about &e (e.g. nodes carried across via std::move(in_.consts) into
    // out_), back-fill both directions of the current table so consumers
    // (mlir_gen, borrow_check) that reverse-lookup by offset can find &e —
    // and fall through to the variant walk in dry-run mode so each child
    // emit_* call back-fills its own descendants.
    bool backfill_only = false;
    if (e.mirror_offset_ != hermes::arena_offset_t{}) {
        if (auto it = table_.expr.find(&e); it != table_.expr.end()) {
            return it->second;
        }
        table_.expr[&e] = e.mirror_offset_;
        table_.expr_by_offset[e.mirror_offset_.value()] = &e;
        backfill_only = true;
    } else if (auto it = table_.expr.find(&e); it != table_.expr.end()) {
        // Address-cache hit but the LExpr's own mirror_offset_ is unset:
        // this is heap-address recycling after a prior LExpr was deleted.
        // The cached offset points to the *old* arena map, which is stale
        // for this fresh node. Invalidate and fall through to emit a new
        // mirror for &e. (See line 884 comment: field-as-truth comes
        // first; the address cache is only valid when mirror_offset_ also
        // matches.)
        table_.expr_by_offset.erase(uint32_t(it->second.value()));
        table_.expr.erase(it);
    }

    bool save_dry = dry_run_;
    if (backfill_only) dry_run_ = true;

    using namespace lir;
    hermes::arena_offset_t map_off;
    std::visit([&](auto const& alt) {
        using T = std::decay_t<decltype(alt)>;
        using Code = lir_schema::expr::Code;

        if constexpr (std::is_same_v<T, ELitInt>) {
            map_off = make_map(hermes::schema::lir_expr(Code::LitInt));
            put(map_off, ek::LIT_I64, put_i64(alt.value));
        } else if constexpr (std::is_same_v<T, ELitFloat>) {
            map_off = make_map(hermes::schema::lir_expr(Code::LitFloat));
            put(map_off, ek::LIT_F64, put_f64(alt.value));
        } else if constexpr (std::is_same_v<T, ELitBool>) {
            map_off = make_map(hermes::schema::lir_expr(Code::LitBool));
            put(map_off, ek::LIT_BOOL, put_bool(alt.value));
        } else if constexpr (std::is_same_v<T, ELitStr>) {
            auto s_av = put_string(alt.value);
            map_off = make_map(hermes::schema::lir_expr(Code::LitStr));
            put(map_off, ek::LIT_STR, s_av);
        } else if constexpr (std::is_same_v<T, EVarRef>) {
            auto n_av = put_string(alt.name);
            map_off = make_map(hermes::schema::lir_expr(Code::VarRef));
            put(map_off, ek::NAME, n_av);
        } else if constexpr (std::is_same_v<T, EEnumLit>) {
            auto en_av = put_string(alt.enum_name);
            auto vr_av = put_string(alt.variant);
            map_off = make_map(hermes::schema::lir_expr(Code::EnumLit));
            put(map_off, ek::ENUM_NAME, en_av);
            put(map_off, ek::VARIANT,   vr_av);
            put(map_off, ek::DISC,      put_i64(alt.disc));
        } else if constexpr (std::is_same_v<T, EEnumLitData>) {
            auto en_av  = put_string(alt.enum_name);
            auto vr_av  = put_string(alt.variant);
            auto pl_av  = expr_array(alt.payload);
            map_off = make_map(hermes::schema::lir_expr(Code::EnumLitData));
            put(map_off, ek::ENUM_NAME, en_av);
            put(map_off, ek::VARIANT,   vr_av);
            put(map_off, ek::DISC,      put_i64(alt.disc));
            put(map_off, ek::PAYLOAD,   pl_av);
        } else if constexpr (std::is_same_v<T, ECall>) {
            auto cn_av  = put_string(alt.callee);
            auto ta_av  = type_array(alt.type_args);
            auto ar_av  = expr_array(alt.args);
            map_off = make_map(hermes::schema::lir_expr(Code::Call));
            put(map_off, ek::CALLEE,     cn_av);
            put(map_off, ek::TYPE_ARGS,  ta_av);
            put(map_off, ek::ARGS,       ar_av);
        } else if constexpr (std::is_same_v<T, EMethodCall>) {
            auto recv_av = expr_av(alt.receiver);
            auto m_av    = put_string(alt.method);
            auto ta_av   = type_array(alt.type_args);
            auto ar_av   = expr_array(alt.args);
            map_off = make_map(hermes::schema::lir_expr(Code::MethodCall));
            put(map_off, ek::RECEIVER,        recv_av);
            put(map_off, ek::METHOD,          m_av);
            put(map_off, ek::TYPE_ARGS,       ta_av);
            put(map_off, ek::ARGS,            ar_av);
            put(map_off, ek::VTABLE_INDEX,    put_i32(alt.vtable_index));
            if (!alt.resolved_symbol.empty())
                put(map_off, ek::RESOLVED_SYMBOL, put_string(alt.resolved_symbol));
            if (!alt.resolved_type.empty())
                put(map_off, ek::RESOLVED_TYPE, put_string(alt.resolved_type));
            if (!alt.tag_system.empty())
                put(map_off, ek::TAG_SYSTEM, put_string(alt.tag_system));
            if (!alt.tag_trait.empty())
                put(map_off, ek::TAG_TRAIT, put_string(alt.tag_trait));
        } else if constexpr (std::is_same_v<T, EBinOp>) {
            auto op_av = put_string(alt.op);
            auto l_av  = expr_av(alt.lhs);
            auto r_av  = expr_av(alt.rhs);
            map_off = make_map(hermes::schema::lir_expr(Code::BinOp));
            put(map_off, ek::OP,  op_av);
            put(map_off, ek::LHS, l_av);
            put(map_off, ek::RHS, r_av);
        } else if constexpr (std::is_same_v<T, EUnary>) {
            auto op_av = put_string(alt.op);
            auto o_av  = expr_av(alt.operand);
            map_off = make_map(hermes::schema::lir_expr(Code::Unary));
            put(map_off, ek::OP,      op_av);
            put(map_off, ek::OPERAND, o_av);
        } else if constexpr (std::is_same_v<T, EAddrOf>) {
            auto n_av = put_string(alt.var_name);
            map_off = make_map(hermes::schema::lir_expr(Code::AddrOf));
            put(map_off, ek::NAME, n_av);
        } else if constexpr (std::is_same_v<T, EAddrOfTemp>) {
            auto in_av = expr_av(alt.inner);
            map_off = make_map(hermes::schema::lir_expr(Code::AddrOfTemp));
            put(map_off, ek::INNER,  in_av);
            put(map_off, ek::IS_MUT, put_bool(alt.is_mut));
        } else if constexpr (std::is_same_v<T, EDeref>) {
            auto o_av = expr_av(alt.operand);
            map_off = make_map(hermes::schema::lir_expr(Code::Deref));
            put(map_off, ek::OPERAND, o_av);
        } else if constexpr (std::is_same_v<T, EFieldRead>) {
            auto r_av = expr_av(alt.receiver);
            auto f_av = put_string(alt.field);
            map_off = make_map(hermes::schema::lir_expr(Code::FieldRead));
            put(map_off, ek::RECEIVER, r_av);
            put(map_off, ek::NAME,     f_av);
        } else if constexpr (std::is_same_v<T, EIndexRead>) {
            auto r_av = expr_av(alt.receiver);
            auto i_av = expr_av(alt.index);
            map_off = make_map(hermes::schema::lir_expr(Code::IndexRead));
            put(map_off, ek::RECEIVER, r_av);
            put(map_off, ek::INDEX,    i_av);
        } else if constexpr (std::is_same_v<T, EStructLit>) {
            auto n_av = put_string(alt.name);
            auto fa   = struct_fields(alt.fields);
            map_off = make_map(hermes::schema::lir_expr(Code::StructLit));
            put(map_off, ek::STRUCT_NAME,  n_av);
            put(map_off, ek::FIELD_NAMES,  fa.names);
            put(map_off, ek::FIELD_VALUES, fa.values);
        } else if constexpr (std::is_same_v<T, EArrLit>) {
            auto el_av = expr_array(alt.elems);
            map_off = make_map(hermes::schema::lir_expr(Code::ArrLit));
            put(map_off, ek::ELEMS, el_av);
        } else if constexpr (std::is_same_v<T, ECast>) {
            auto o_av = expr_av(alt.operand);
            map_off = make_map(hermes::schema::lir_expr(Code::Cast));
            put(map_off, ek::OPERAND, o_av);
            if (!alt.hermes_build_fn.empty())
                put(map_off, ek::HERMES_BUILD_FN, put_string(alt.hermes_build_fn));
        } else if constexpr (std::is_same_v<T, ENew>) {
            auto n_av = put_string(alt.class_name);
            auto fa   = struct_fields(alt.fields);
            map_off = make_map(hermes::schema::lir_expr(Code::New));
            put(map_off, ek::CLASS_NAME,   n_av);
            put(map_off, ek::FIELD_NAMES,  fa.names);
            put(map_off, ek::FIELD_VALUES, fa.values);
        } else if constexpr (std::is_same_v<T, EIfExpr>) {
            auto c_av = expr_av(alt.cond);
            auto t_av = expr_av(alt.then_val);
            auto e_av = expr_av(alt.else_val);
            map_off = make_map(hermes::schema::lir_expr(Code::IfExpr));
            put(map_off, ek::COND,     c_av);
            put(map_off, ek::THEN_VAL, t_av);
            put(map_off, ek::ELSE_VAL, e_av);
        } else if constexpr (std::is_same_v<T, ETupleLit>) {
            auto el_av = expr_array(alt.elems);
            map_off = make_map(hermes::schema::lir_expr(Code::TupleLit));
            put(map_off, ek::ELEMS, el_av);
        } else if constexpr (std::is_same_v<T, ETupleIndex>) {
            auto r_av = expr_av(alt.receiver);
            map_off = make_map(hermes::schema::lir_expr(Code::TupleIndex));
            put(map_off, ek::RECEIVER,        r_av);
            put(map_off, ek::TUPLE_INDEX_VAL, put_u32(alt.index));
        } else if constexpr (std::is_same_v<T, ESliceLit>) {
            auto b_av = expr_av(alt.base);
            auto l_av = expr_av(alt.len);
            map_off = make_map(hermes::schema::lir_expr(Code::SliceLit));
            put(map_off, ek::BASE_PTR, b_av);
            put(map_off, ek::LEN,      l_av);
        } else if constexpr (std::is_same_v<T, ESliceIndex>) {
            auto s_av = expr_av(alt.slice);
            auto i_av = expr_av(alt.index);
            map_off = make_map(hermes::schema::lir_expr(Code::SliceIndex));
            put(map_off, ek::SLICE, s_av);
            put(map_off, ek::INDEX, i_av);
        } else if constexpr (std::is_same_v<T, ESliceLen>) {
            auto s_av = expr_av(alt.slice);
            map_off = make_map(hermes::schema::lir_expr(Code::SliceLen));
            put(map_off, ek::SLICE, s_av);
        } else if constexpr (std::is_same_v<T, ESlicePtr>) {
            auto s_av = expr_av(alt.slice);
            map_off = make_map(hermes::schema::lir_expr(Code::SlicePtr));
            put(map_off, ek::SLICE, s_av);
        } else if constexpr (std::is_same_v<T, EClosureBox>) {
            auto cl_av = closure_av(*alt.inner);
            map_off = make_map(hermes::schema::lir_expr(Code::ClosureBox));
            put(map_off, ek::CLOSURE, cl_av);
        } else if constexpr (std::is_same_v<T, EClosureCall>) {
            auto c_av = expr_av(alt.callee);
            auto a_av = expr_array(alt.args);
            map_off = make_map(hermes::schema::lir_expr(Code::ClosureCall));
            put(map_off, ek::CALLEE, c_av);
            put(map_off, ek::ARGS,   a_av);
        } else if constexpr (std::is_same_v<T, EFnPtrCall>) {
            auto c_av = expr_av(alt.callee);
            auto a_av = expr_array(alt.args);
            map_off = make_map(hermes::schema::lir_expr(Code::FnPtrCall));
            put(map_off, ek::CALLEE, c_av);
            put(map_off, ek::ARGS,   a_av);
        } else if constexpr (std::is_same_v<T, EFormatCall>) {
            auto f_av  = expr_av(alt.fmt);
            auto a_av  = expr_array(alt.args);
            auto at_av = type_array(alt.arg_types);
            map_off = make_map(hermes::schema::lir_expr(Code::FormatCall));
            put(map_off, ek::FMT,       f_av);
            put(map_off, ek::ARGS,      a_av);
            put(map_off, ek::ARG_TYPES, at_av);
        } else if constexpr (std::is_same_v<T, EPackExpand>) {
            auto n_av = put_string(alt.var_name);
            map_off = make_map(hermes::schema::lir_expr(Code::PackExpand));
            put(map_off, ek::NAME, n_av);
        } else if constexpr (std::is_same_v<T, ETry>) {
            auto in_av = expr_av(alt.inner);
            map_off = make_map(hermes::schema::lir_expr(Code::Try));
            put(map_off, ek::INNER,    in_av);
            put(map_off, ek::OK_DISC,  put_i32(alt.ok_disc));
            put(map_off, ek::ERR_DISC, put_i32(alt.err_disc));
        } else if constexpr (std::is_same_v<T, EMatchExpr>) {
            auto sc_av = expr_av(alt.scrut);
            auto ar_av = expr_arm_array(alt.arms);
            map_off = make_map(hermes::schema::lir_expr(Code::MatchExpr));
            put(map_off, ek::SCRUT, sc_av);
            put(map_off, ek::ARMS,  ar_av);
        } else if constexpr (std::is_same_v<T, ESizeOf>) {
            map_off = make_map(hermes::schema::lir_expr(Code::SizeOf));
            put(map_off, ek::ELEM_TYPE, type_av(alt.elem_type));
        } else if constexpr (std::is_same_v<T, ETypeCodeOf>) {
            map_off = make_map(hermes::schema::lir_expr(Code::TypeCodeOf));
            put(map_off, ek::ELEM_TYPE, type_av(alt.elem_type));
        } else if constexpr (std::is_same_v<T, EBlockExpr>) {
            auto b_av = block_av_raw(alt.block.get());
            auto r_av = expr_av(alt.result);
            map_off = make_map(hermes::schema::lir_expr(Code::BlockExpr));
            put(map_off, ek::BLOCK,  b_av);
            put(map_off, ek::RESULT, r_av);
        } else if constexpr (std::is_same_v<T, EHermesLit>) {
            auto root_av = hv_av(alt.root);
            auto cap_ex  = expr_array(alt.capture_exprs);
            auto cap_ty  = type_array(alt.capture_types);
            map_off = make_map(hermes::schema::lir_expr(Code::HermesLit));
            put(map_off, hl::ROOT,                  root_av);
            put(map_off, hl::HAS_CAPTURES,          put_bool(alt.has_captures));
            put(map_off, hl::CAPTURE_EXPRS,         cap_ex);
            put(map_off, hl::CAPTURE_TYPES,         cap_ty);
            put(map_off, hl::CAPTURE_PARAM_COUNT,   put_u32(alt.capture_param_count));
        } else if constexpr (std::is_same_v<T, EPtrArith>) {
            auto p_av = expr_av(alt.ptr);
            auto o_av = expr_av(alt.offset);
            map_off = make_map(hermes::schema::lir_expr(Code::PtrArith));
            put(map_off, ek::PTR_ARITH_OP, put_u8(uint8_t(alt.op)));
            put(map_off, ek::BASE_PTR,     p_av);
            put(map_off, ek::OFFSET,       o_av);
        } else if constexpr (std::is_same_v<T, EPtrDiff>) {
            auto l_av = expr_av(alt.lhs);
            auto r_av = expr_av(alt.rhs);
            map_off = make_map(hermes::schema::lir_expr(Code::PtrDiff));
            put(map_off, pdk::BY_BYTE, put_bool(alt.by_byte));
            put(map_off, ek::LHS,     l_av);
            put(map_off, ek::RHS,     r_av);
        } else if constexpr (std::is_same_v<T, EReflectOf>) {
            map_off = make_map(hermes::schema::lir_expr(Code::ReflectOf));
            put(map_off, ek::ELEM_TYPE, type_av(alt.type));
        }
    }, e.kind);

    if (e.type) put(map_off, ec::TYPE, type_av(e.type));

    dry_run_ = save_dry;
    if (backfill_only) return e.mirror_offset_;

    e.mirror_offset_ = map_off;
    table_.expr[&e] = map_off;
    table_.expr_by_offset[map_off.value()] = &e;
    return map_off;
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
    LirMirrorEmitter em(arena, table);
    em.run(prog);
    return table;
}

void lir_mirror_emit_function(lir::LProgram& prog,
                              LirMirrorTable& table,
                              lir::LFunction& fn) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, table);
    em.emit_function(fn);
}

void lir_mirror_emit_into(lir::LProgram& prog, LirMirrorTable& table) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, table);
    em.run(prog);
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
    LirMirrorEmitter em(arena, *prog.mirror_table);
    return em.emit_expr_public(e);
}
hermes::arena_offset_t lir_mirror_emit_stmt_node(lir::LProgram& prog, const lir::LStmt& s) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table);
    return em.emit_stmt_public(s);
}
hermes::arena_offset_t lir_mirror_emit_block_node(lir::LProgram& prog, const lir::LBlock& b) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table);
    return em.emit_block_public(b);
}
hermes::arena_offset_t lir_mirror_emit_pat_node(lir::LProgram& prog, const lir::Pattern& p) {
    auto& arena = prog.type_pool.arena_or_init();
    LirMirrorEmitter em(arena, *prog.mirror_table);
    return em.emit_pat_public(p);
}

} // namespace logos::compiler
