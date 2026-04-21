// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// clone.cpp — port of `document_compactify` from stdlib/hermes/clone.logos.
// Per-type clone_tagged handlers are registered into TypeOps by stringify.cpp
// via forward declarations (see clone_impl below).

#include <logos/hermes/clone.hpp>
#include <logos/hermes/access.hpp>
#include <logos/hermes/type_ops.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/object_map.hpp>
#include <logos/hermes/compound_types.hpp>
#include <logos/hermes/type_tag.hpp>
#include <logos/hermes/type_registry.hpp>
#include <logos/hermes/varint.hpp>
#include <logos/core/err.hpp>
#include <logos/verification/assert.hpp>

#include <cstring>

namespace logos::hermes {

namespace clone_impl {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static inline uint32_t dst_offset_of(const void* p, Arena& dst) noexcept {
    return static_cast<uint32_t>(
        static_cast<const uint8_t*>(p) - dst.head().data());
}

static inline uint8_t* dst_ptr(uint32_t off, Arena& dst) noexcept {
    return dst.head().data() + off;
}

// Register src_off → dst_off in the cycle cache before recursing into
// children. Call sites MUST insert before recursion to break cycles.
static inline void remember(CloneCtx* ctx, uint32_t src_off, uint32_t dst_off) noexcept {
    ctx->map.emplace(src_off, dst_off);
}

// Compute src_off of an object pointer inside src arena.
static inline uint32_t src_off_of(const void* obj, const uint8_t* base_src) noexcept {
    return static_cast<uint32_t>(
        static_cast<const uint8_t*>(obj) - base_src);
}

// PARAM recognition: AnyVal inline value with type_code == 127.
// Raw layout: bit0=1 (inline), bits[7:1]=tc, bits[31:8]=payload.
// So tc==127 inline ⇔ (raw & 0xFF) == 0xFF.
static inline bool is_param(uint32_t raw) noexcept {
    return (raw & 0xFFu) == 0xFFu;
}

static inline uint32_t param_value_index(uint32_t raw) noexcept {
    return raw >> 8;
}

// Record a PARAM at a freshly-written dst slot, if ctx tracks them and the
// value is indeed a PARAM. `dst_slot_off` is the byte offset of the AnyVal
// slot inside the dst arena.
static inline void
maybe_record_param(CloneCtx* c, uint32_t raw, uint32_t dst_slot_off) noexcept {
    if (c->out_params && is_param(raw)) {
        c->out_params->push_back(ParamSlot{dst_slot_off, param_value_index(raw)});
    }
}

// ---------------------------------------------------------------------------
// Fixed-size "data" clone: just alloc+memcpy, same tag.
// ---------------------------------------------------------------------------

template <size_t N, size_t Align>
static logos::expected<uint32_t>
clone_fixed(const uint8_t* obj, CloneCtx* ctx, uint64_t type_code) noexcept {
    uint32_t src_off = src_off_of(obj, ctx->base_src);
    TypeTag tag(type_code, TagDescriptor::Data);
    LOGOS_TRY(auto* mem_void, ctx->dst->allocate(N, Align, tag));
    auto* mem = static_cast<uint8_t*>(mem_void);
    std::memcpy(mem, obj, N);
    uint32_t dst_off = dst_offset_of(mem, *ctx->dst);
    remember(ctx, src_off, dst_off);
    return dst_off;
}

// ---------------------------------------------------------------------------
// Scalar / fixed-size clone handlers.
// Most scalars live inline in AnyVal and never hit pointer mode, but we
// register handlers for completeness.
// ---------------------------------------------------------------------------

logos::expected<uint32_t> c_tinyint   (const uint8_t* o, CloneCtx* c) noexcept { return clone_fixed<1, 2>(o, c, type_hash::I8); }
logos::expected<uint32_t> c_utinyint  (const uint8_t* o, CloneCtx* c) noexcept { return clone_fixed<1, 2>(o, c, type_hash::U8); }
logos::expected<uint32_t> c_smallint  (const uint8_t* o, CloneCtx* c) noexcept { return clone_fixed<2, 2>(o, c, type_hash::I16); }
logos::expected<uint32_t> c_usmallint (const uint8_t* o, CloneCtx* c) noexcept { return clone_fixed<2, 2>(o, c, type_hash::U16); }
logos::expected<uint32_t> c_integer   (const uint8_t* o, CloneCtx* c) noexcept { return clone_fixed<4, 4>(o, c, type_hash::I24); }
logos::expected<uint32_t> c_uinteger  (const uint8_t* o, CloneCtx* c) noexcept { return clone_fixed<4, 4>(o, c, type_hash::U24); }
logos::expected<uint32_t> c_bigint    (const uint8_t* o, CloneCtx* c) noexcept { return clone_fixed<8, 8>(o, c, type_hash::I64); }
logos::expected<uint32_t> c_ubigint   (const uint8_t* o, CloneCtx* c) noexcept { return clone_fixed<8, 8>(o, c, type_hash::U64); }
logos::expected<uint32_t> c_real      (const uint8_t* o, CloneCtx* c) noexcept { return clone_fixed<4, 4>(o, c, type_hash::F32); }
logos::expected<uint32_t> c_double    (const uint8_t* o, CloneCtx* c) noexcept { return clone_fixed<8, 8>(o, c, type_hash::F64); }
logos::expected<uint32_t> c_boolean   (const uint8_t* o, CloneCtx* c) noexcept { return clone_fixed<1, 2>(o, c, type_hash::Bool); }

// ---------------------------------------------------------------------------
// Varchar: vlen header + bytes, raw memcpy.
// ---------------------------------------------------------------------------

logos::expected<uint32_t> c_varchar(const uint8_t* o, CloneCtx* c) noexcept {
    uint32_t src_off = src_off_of(o, c->base_src);
    VarIntResult vr = varint_decode(o);
    size_t total = vr.bytes_read + vr.value;
    TypeTag tag(type_hash::HermesString, TagDescriptor::Data);
    LOGOS_TRY(auto* mem_void, c->dst->allocate(total, 2, tag));
    std::memcpy(mem_void, o, total);
    uint32_t dst_off = dst_offset_of(mem_void, *c->dst);
    remember(c, src_off, dst_off);
    return dst_off;
}

// ---------------------------------------------------------------------------
// ObjectArray: header + elements buffer.
// Mirrors Logos Array<AnyVal> clone — register self, then clone each slot.
// ---------------------------------------------------------------------------

logos::expected<uint32_t> c_object_array(const uint8_t* o, CloneCtx* c) noexcept {
    auto* src = reinterpret_cast<const ObjectArray*>(o);
    uint32_t src_off = src_off_of(o, c->base_src);
    uint64_t n = src->size();

    // Allocate destination ObjectArray with the same (current) size so that
    // slot(i, base) is valid for indices [0, n). size_/capacity_ layout matches.
    LOGOS_TRY(auto* dst_arr, ObjectArray::create(*c->dst, n));
    uint32_t dst_off = dst_offset_of(dst_arr, *c->dst);
    remember(c, src_off, dst_off);

    // Pre-fill slots with null then push_back to get size_ to match.
    for (uint64_t i = 0; i < n; ++i) {
        // Re-fetch dst_arr — previous push_back may have grown the arena.
        auto* cur = reinterpret_cast<ObjectArray*>(dst_ptr(dst_off, *c->dst));
        LOGOS_TRY_VOID(cur->push_back(AnyVal{}, *c->dst));
    }

    // Now clone each AnyVal slot.
    auto* src_mut = const_cast<ObjectArray*>(src);
    for (uint64_t i = 0; i < n; ++i) {
        AnyVal* src_slot = src_mut->slot(i, const_cast<uint8_t*>(c->base_src));
        uint32_t src_raw = src_slot->raw();
        LOGOS_TRY(auto new_raw, anyval_clone(src_raw, c));
        // Recompute dst pointers after recursion — arena may have grown.
        auto* dst_arr_now = reinterpret_cast<ObjectArray*>(dst_ptr(dst_off, *c->dst));
        AnyVal* dst_slot = dst_arr_now->slot(i, c->dst->head().data());
        *dst_slot = AnyVal::from_raw(new_raw);
        uint32_t dst_slot_off = dst_offset_of(dst_slot, *c->dst);
        maybe_record_param(c, new_raw, dst_slot_off);
    }
    return dst_off;
}

// ---------------------------------------------------------------------------
// TinyObjectMap: bitmap-indexed.
// ---------------------------------------------------------------------------

logos::expected<uint32_t> c_tiny_map(const uint8_t* o, CloneCtx* c) noexcept {
    auto* src = reinterpret_cast<const TinyObjectMap*>(o);
    uint32_t src_off = src_off_of(o, c->base_src);
    uint64_t bm = src->bitmap();

    LOGOS_TRY(auto* dst_map, TinyObjectMap::create(*c->dst, src->capacity()));
    uint32_t dst_off = dst_offset_of(dst_map, *c->dst);
    remember(c, src_off, dst_off);

    for (uint8_t key = 0; key < TinyObjectMap::MAX_KEYS; ++key) {
        if (!(bm & (1ULL << key))) continue;
        // Put null first to create slot; then recurse, then overwrite.
        {
            auto* cur = reinterpret_cast<TinyObjectMap*>(dst_ptr(dst_off, *c->dst));
            LOGOS_TRY_VOID(cur->put(key, AnyVal{}, *c->dst));
        }
        const AnyVal* src_slot = src->slot(key, c->base_src);
        uint32_t src_raw = src_slot->raw();
        LOGOS_TRY(auto new_raw, anyval_clone(src_raw, c));
        auto* cur = reinterpret_cast<TinyObjectMap*>(dst_ptr(dst_off, *c->dst));
        AnyVal* dst_slot = cur->slot(key, c->dst->head().data());
        *dst_slot = AnyVal::from_raw(new_raw);
        uint32_t dst_slot_off = dst_offset_of(dst_slot, *c->dst);
        maybe_record_param(c, new_raw, dst_slot_off);
    }
    return dst_off;
}

// ---------------------------------------------------------------------------
// ObjectMap: string-keyed.
// ---------------------------------------------------------------------------

logos::expected<uint32_t> c_object_map(const uint8_t* o, CloneCtx* c) noexcept {
    auto* src = reinterpret_cast<const ObjectMap*>(o);
    uint32_t src_off = src_off_of(o, c->base_src);

    LOGOS_TRY(auto* dst_map, ObjectMap::create(*c->dst));
    uint32_t dst_off = dst_offset_of(dst_map, *c->dst);
    remember(c, src_off, dst_off);

    logos::expected<void> status{};
    src->for_each([&](ArenaString* key, AnyVal* val_slot) noexcept {
        if (!status) return;
        uint32_t src_raw = val_slot->raw();
        // Capture the string_view eagerly (points into src arena, stable).
        std::string_view kv = key->view();

        // Insert with null so rehash/grow happens under our control.
        auto* cur = reinterpret_cast<ObjectMap*>(dst_ptr(dst_off, *c->dst));
        auto r1 = cur->put(kv, AnyVal{}, *c->dst);
        if (!r1) { status = std::move(r1); return; }

        auto nr = anyval_clone(src_raw, c);
        if (!nr) { status = std::unexpected(std::move(nr.error())); return; }

        auto* cur2 = reinterpret_cast<ObjectMap*>(dst_ptr(dst_off, *c->dst));
        AnyVal* dst_slot = cur2->get_slot(kv, c->dst->head().data());
        if (dst_slot) {
            *dst_slot = AnyVal::from_raw(*nr);
            uint32_t dst_slot_off = dst_offset_of(dst_slot, *c->dst);
            maybe_record_param(c, *nr, dst_slot_off);
        }
    }, const_cast<uint8_t*>(c->base_src));

    if (!status) return std::unexpected(std::move(status.error()));
    return dst_off;
}

// ---------------------------------------------------------------------------
// Datatype
// ---------------------------------------------------------------------------

logos::expected<uint32_t> c_datatype(const uint8_t* o, CloneCtx* c) noexcept {
    auto* src = reinterpret_cast<const DatatypeData*>(o);
    uint32_t src_off = src_off_of(o, c->base_src);

    // Snapshot src offsets/flags before any dst allocation.
    uint8_t* src_base = const_cast<uint8_t*>(c->base_src);
    const ArenaString* src_name = src->name.get(src_base);
    const ObjectArray* src_params = src->has_params() ? src->params.get(src_base) : nullptr;
    const ObjectArray* src_ctr = src->has_ctr() ? src->ctr.get(src_base) : nullptr;
    uint32_t extras_val = src->extras;

    // Allocate dst header first, register cycle entry, then clone children.
    TypeTag tag(type_hash::Datatype, TagDescriptor::Data);
    LOGOS_TRY(auto* mem_void, c->dst->allocate(sizeof(DatatypeData), alignof(DatatypeData), tag));
    uint32_t dst_off = dst_offset_of(mem_void, *c->dst);
    {
        auto* init_dt = new (mem_void) DatatypeData();
        init_dt->extras = 0;
        init_dt->name.clear();
        init_dt->params.clear();
        init_dt->ctr.clear();
    }
    remember(c, src_off, dst_off);

    // Clone name (guaranteed Varchar-tagged in src).
    LOGOS_TRY(auto name_raw, anyval_clone(AnyVal::from_offset(
        arena_offset_t(src_off_of(src_name, c->base_src))).raw(), c));
    uint32_t name_off = AnyVal::from_raw(name_raw).to_offset().value();

    uint32_t params_off = 0;
    if (src_params) {
        LOGOS_TRY(auto p_raw, anyval_clone(AnyVal::from_offset(
            arena_offset_t(src_off_of(src_params, c->base_src))).raw(), c));
        params_off = AnyVal::from_raw(p_raw).to_offset().value();
    }
    uint32_t ctr_off = 0;
    if (src_ctr) {
        LOGOS_TRY(auto ct_raw, anyval_clone(AnyVal::from_offset(
            arena_offset_t(src_off_of(src_ctr, c->base_src))).raw(), c));
        ctr_off = AnyVal::from_raw(ct_raw).to_offset().value();
    }

    // Recompute dst pointer after child clones (arena may have grown).
    uint8_t* dst_base = c->dst->head().data();
    auto* dst_dt = reinterpret_cast<DatatypeData*>(dst_base + dst_off);
    dst_dt->extras = extras_val;
    dst_dt->name.set(reinterpret_cast<ArenaString*>(dst_base + name_off), dst_base);
    if (params_off)
        dst_dt->params.set(reinterpret_cast<ObjectArray*>(dst_base + params_off), dst_base);
    if (ctr_off)
        dst_dt->ctr.set(reinterpret_cast<ObjectArray*>(dst_base + ctr_off), dst_base);
    return dst_off;
}

// ---------------------------------------------------------------------------
// TypedValue
// ---------------------------------------------------------------------------

logos::expected<uint32_t> c_typed_value(const uint8_t* o, CloneCtx* c) noexcept {
    auto* src = reinterpret_cast<const TypedValueData*>(o);
    uint32_t src_off = src_off_of(o, c->base_src);

    uint8_t* src_base = const_cast<uint8_t*>(c->base_src);
    const DatatypeData* src_dt = src->datatype.get(src_base);
    uint32_t src_val_raw = src->value.raw();

    TypeTag tag(type_hash::TypedValue, TagDescriptor::Data);
    LOGOS_TRY(auto* mem_void, c->dst->allocate(sizeof(TypedValueData), alignof(TypedValueData), tag));
    uint32_t dst_off = dst_offset_of(mem_void, *c->dst);
    {
        auto* init_tv = new (mem_void) TypedValueData();
        init_tv->datatype.clear();
        init_tv->value = AnyVal{};
    }
    remember(c, src_off, dst_off);

    LOGOS_TRY(auto dt_raw, anyval_clone(AnyVal::from_offset(
        arena_offset_t(src_off_of(src_dt, c->base_src))).raw(), c));
    uint32_t dt_off = AnyVal::from_raw(dt_raw).to_offset().value();

    LOGOS_TRY(auto new_val_raw, anyval_clone(src_val_raw, c));

    uint8_t* dst_base = c->dst->head().data();
    auto* dst_tv = reinterpret_cast<TypedValueData*>(dst_base + dst_off);
    dst_tv->datatype.set(reinterpret_cast<DatatypeData*>(dst_base + dt_off), dst_base);
    dst_tv->value = AnyVal::from_raw(new_val_raw);
    uint32_t dst_slot_off = dst_offset_of(&dst_tv->value, *c->dst);
    maybe_record_param(c, new_val_raw, dst_slot_off);
    return dst_off;
}

// ---------------------------------------------------------------------------
// Decimal: variable-length (header + nlimbs × u32). No RelativePtr fields.
// ---------------------------------------------------------------------------

logos::expected<uint32_t> c_decimal(const uint8_t* o, CloneCtx* c) noexcept {
    auto* src = reinterpret_cast<const DecimalData*>(o);
    uint32_t src_off = src_off_of(o, c->base_src);
    size_t sz = src->byte_size();
    TypeTag tag(type_hash::Decimal, TagDescriptor::Data);
    LOGOS_TRY(auto* mem_void, c->dst->allocate(sz, alignof(uint32_t), tag));
    std::memcpy(mem_void, o, sz);
    uint32_t dst_off = dst_offset_of(mem_void, *c->dst);
    remember(c, src_off, dst_off);
    return dst_off;
}

// ---------------------------------------------------------------------------
// Parameter
// ---------------------------------------------------------------------------

logos::expected<uint32_t> c_parameter(const uint8_t* o, CloneCtx* c) noexcept {
    auto* src = reinterpret_cast<const ParameterData*>(o);
    uint32_t src_off = src_off_of(o, c->base_src);

    uint8_t* src_base = const_cast<uint8_t*>(c->base_src);
    const ArenaString* src_name = src->name.get(src_base);

    TypeTag tag(type_hash::Parameter, TagDescriptor::Data);
    LOGOS_TRY(auto* mem_void, c->dst->allocate(sizeof(ParameterData), alignof(ParameterData), tag));
    uint32_t dst_off = dst_offset_of(mem_void, *c->dst);
    {
        auto* init_p = new (mem_void) ParameterData();
        init_p->name.clear();
    }
    remember(c, src_off, dst_off);

    LOGOS_TRY(auto name_raw, anyval_clone(AnyVal::from_offset(
        arena_offset_t(src_off_of(src_name, c->base_src))).raw(), c));
    uint32_t name_off = AnyVal::from_raw(name_raw).to_offset().value();

    uint8_t* dst_base = c->dst->head().data();
    auto* dst_p = reinterpret_cast<ParameterData*>(dst_base + dst_off);
    dst_p->name.set(reinterpret_cast<ArenaString*>(dst_base + name_off), dst_base);
    return dst_off;
}

} // namespace clone_impl

// ---------------------------------------------------------------------------
// anyval_clone — public dispatch entry point.
// ---------------------------------------------------------------------------

logos::expected<uint32_t> anyval_clone(uint32_t src_raw, CloneCtx* ctx) noexcept {
    AnyVal v = AnyVal::from_raw(src_raw);

    if (v.is_null()) return uint32_t{0};

    if (v.is_value()) {
        // PARAM (tc=127) tracking: the dst-arena offset of the slot is known
        // only at the parent write site, so each container records the slot
        // offset via maybe_record_param() after writing the raw. Here we just
        // pass the raw through — inline values are self-describing.
        return src_raw;
    }

    // Pointer mode.
    uint32_t src_off = v.to_offset().value();
    if (src_off == 0) return uint32_t{0};

    auto it = ctx->map.find(src_off);
    if (it != ctx->map.end()) {
        return AnyVal::from_offset(arena_offset_t(it->second)).raw();
    }

    const uint8_t* obj = ctx->base_src + src_off;
    TypeTag tag = TypeTag::read_before(obj);
    const TypeOps* ops = find_type_ops(tag.type_code());
    if (!ops || !ops->clone_tagged) {
        return std::unexpected(logos::err(ErrCode::out_of_memory));
    }
    LOGOS_TRY(auto dst_off, ops->clone_tagged(obj, ctx));
    return AnyVal::from_offset(arena_offset_t(dst_off)).raw();
}

// ---------------------------------------------------------------------------
// clone() — public API.
// ---------------------------------------------------------------------------

logos::expected<Hermes> clone(const HermesView& src,
                              std::vector<ParamSlot>* out_params) noexcept {
    // Size hint: src_used is a tight lower bound (packed output). Growing
    // mid-clone is supported — all per-type handlers recompute `this` from
    // (base + self_off) after every child allocation, and all containers
    // (ObjectArray / ObjectMap / TinyObjectMap) use self-offset recompute
    // across their grow/rehash/push paths.
    size_t src_used = HermesAccess::arena(src).total_used();
    size_t cap = src_used < 64 ? 64 : src_used;

    LOGOS_TRY(auto dst, make_doc(cap));

    CloneCtx ctx;
    ctx.base_src   = HermesAccess::base(src);
    ctx.dst        = &HermesAccess::arena(dst);
    ctx.out_params = out_params;

    const auto* src_hdr = reinterpret_cast<const DocumentHeader*>(ctx.base_src);
    arena_offset_t src_root_off = src_hdr->root_offset;

    uint32_t src_root_raw = AnyVal::from_offset(src_root_off).raw();
    LOGOS_TRY(auto new_root_raw, anyval_clone(src_root_raw, &ctx));

    AnyVal new_root = AnyVal::from_raw(new_root_raw);
    if (new_root.is_pointer()) {
        // Pointer root (including null/raw=0): write the offset into
        // DocumentHeader.root_offset.
        HermesAccess::set_root_offset(dst, new_root.to_offset());
    } else {
        // Inline root (value-mode AnyVal, bit0=1): write the raw 4 bytes
        // directly at offset 0. DocumentHeader overlaps the AnyVal; the
        // bit0=1 flag disambiguates inline vs pointer on read.
        // Mirrors stdlib document_set_root in stdlib/hermes/document.logos.
        HermesAccess::set_root_offset(dst, arena_offset_t(new_root_raw));
    }
    // If PARAM root: record its slot (offset 0) in out_params.
    if (out_params) {
        clone_impl::maybe_record_param(&ctx, new_root_raw, 0);
    }
    return dst;
}

} // namespace logos::hermes
