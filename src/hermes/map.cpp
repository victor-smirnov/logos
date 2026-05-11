// Logos project — https://github.com/victor-smirnov/logos
//
// map.cpp — TypeOps registration for Map<int32_t, AnyVal> (type_code=105),
// mirroring stdlib/hermes/map.logos Map<i32, AnyVal> plus the stringify /
// clone handlers defined in stringify.logos and clone.logos.

#include <logos/hermes/map.hpp>
#include <logos/hermes/clone.hpp>
#include <logos/hermes/type_ops.hpp>
#include <logos/hermes/type_tag.hpp>
#include <logos/hermes/type_registry.hpp>
#include <logos/hermes/access.hpp>

#include <cstring>
#include <string>

namespace logos::hermes {

// anyval_clone is declared in clone.hpp.

namespace {

// ---------------------------------------------------------------------------
// Stringify: <I32,AnyVal> {k:v,k:v}
// Pretty form adds a single space after ':' per ctx->indent > 0 convention
// used by the Logos reference in stdlib/hermes/stringify.logos.
// ---------------------------------------------------------------------------

template <typename K>
const char* s_map_prefix() noexcept {
    if constexpr (std::is_same_v<K, int32_t>)  return "<I32,AnyVal> {";
    if constexpr (std::is_same_v<K, uint32_t>) return "<U32,AnyVal> {";
    if constexpr (std::is_same_v<K, int64_t>)  return "<I64,AnyVal> {";
    if constexpr (std::is_same_v<K, uint64_t>) return "<U64,AnyVal> {";
    return "<?,AnyVal> {";
}

template <typename K>
logos::expected<void> s_map_k_anyval(const uint8_t* o, StringifyCtx* c) noexcept {
    using MapT = TypedMap<K, AnyVal>;
    auto* m = reinterpret_cast<const MapT*>(o);
    *c->out += s_map_prefix<K>();
    uint32_t n = m->size();
    const K*      keys_ptr = const_cast<MapT*>(m)->keys(c->base);
    const AnyVal* vals_ptr = const_cast<MapT*>(m)->vals(c->base);
    for (uint32_t i = 0; i < n; ++i) {
        if (i > 0) *c->out += ',';
        if constexpr (std::is_signed_v<K>) {
            *c->out += std::to_string(static_cast<int64_t>(keys_ptr[i]));
        } else {
            *c->out += std::to_string(static_cast<uint64_t>(keys_ptr[i]));
        }
        *c->out += ':';
        if (c->pretty) *c->out += ' ';
        AnyVal v = vals_ptr[i];
        LOGOS_TRY_VOID(c->recurse_anyval(&v, c));
    }
    *c->out += '}';
    return {};
}

// ---------------------------------------------------------------------------
// Clone: allocate header + keys[size] + vals[size] (size = src.size(); we pack
// tightly in dst). Register cycle entry immediately after header alloc; clone
// each value through anyval_clone. PARAM slots are tracked after each val
// write, matching c_object_array / c_object_map.
// ---------------------------------------------------------------------------

static inline uint32_t dst_offset_of(const void* p, Arena& dst) noexcept {
    return static_cast<uint32_t>(
        static_cast<const uint8_t*>(p) - dst.head().data());
}
static inline uint32_t src_off_of(const void* obj, const uint8_t* base_src) noexcept {
    return static_cast<uint32_t>(
        static_cast<const uint8_t*>(obj) - base_src);
}
static inline bool is_param(uint32_t raw) noexcept {
    return (raw & 0xFFu) == 0xFFu;
}
static inline uint32_t param_value_index(uint32_t raw) noexcept {
    return raw >> 8;
}

template <typename K>
logos::expected<uint32_t> c_map_k_anyval(const uint8_t* o, CloneCtx* c) noexcept {
    using MapT = TypedMap<K, AnyVal>;
    auto* src = reinterpret_cast<const MapT*>(o);
    uint32_t src_off = src_off_of(o, c->base_src);
    uint32_t n = src->size();

    // Snapshot source key/val buffer offsets before any dst allocation.
    const K* src_keys =
        const_cast<MapT*>(src)->keys(const_cast<uint8_t*>(c->base_src));
    const AnyVal*  src_vals =
        const_cast<MapT*>(src)->vals(const_cast<uint8_t*>(c->base_src));

    // Copy keys into a small temporary vector so subsequent dst allocations
    // that may trigger arena growth on the *source* side (they don't — dst is
    // a separate arena) still leave us safe. Harmless but paranoid. For the
    // dst arena, every allocate_raw call invalidates previous pointers so we
    // recompute buffers from offsets after each allocation.

    // Allocate header in dst.
    TypeTag tag(MapT::type_code_for(), TagDescriptor::Map);
    LOGOS_TRY(auto* hdr_void,
        c->dst->allocate(sizeof(MapT), alignof(MapT), tag));
    auto* hdr_init = new (hdr_void) MapT();
    (void)hdr_init;
    uint32_t dst_off = dst_offset_of(hdr_void, *c->dst);
    c->map.emplace(src_off, dst_off);

    if (n == 0) {
        // Still write size/capacity=0; keys/vals offset stays NULL.
        return dst_off;
    }

    // Allocate keys buffer.
    LOGOS_TRY(auto* keys_void,
        c->dst->allocate_raw(n * sizeof(K), alignof(K)));
    uint32_t keys_off = static_cast<uint32_t>(
        static_cast<uint8_t*>(keys_void) - c->dst->head().data());

    // Allocate vals buffer; arena may have grown, so recompute keys_void
    // later via keys_off.
    LOGOS_TRY(auto* vals_void,
        c->dst->allocate_raw(n * sizeof(AnyVal), alignof(AnyVal)));
    uint32_t vals_off = static_cast<uint32_t>(
        static_cast<uint8_t*>(vals_void) - c->dst->head().data());

    // Write header fields using fresh base (previous allocations settled).
    // struct layout is { u32 size; u32 capacity; u32 keys_off; u32 vals_off }.
    {
        uint8_t* base = c->dst->head().data();
        auto* raw = reinterpret_cast<uint32_t*>(base + dst_off);
        raw[0] = n;          // size
        raw[1] = n;          // capacity
        raw[2] = keys_off;   // keys.offset
        raw[3] = vals_off;   // vals.offset
    }

    // Copy keys raw (trivially copyable).
    {
        uint8_t* base = c->dst->head().data();
        std::memcpy(base + keys_off, src_keys, n * sizeof(K));
    }

    // Clone each value via anyval_clone; write into dst vals buffer; record
    // PARAM slot if tracking is enabled.
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t src_raw = src_vals[i].raw();
        LOGOS_TRY(auto new_raw, anyval_clone(src_raw, c));
        uint8_t* base = c->dst->head().data();
        auto* vslot = reinterpret_cast<AnyVal*>(
            base + vals_off + i * sizeof(AnyVal));
        *vslot = AnyVal::from_raw(new_raw);
        if (c->out_params && is_param(new_raw)) {
            uint32_t slot_off = vals_off + static_cast<uint32_t>(i * sizeof(AnyVal));
            c->out_params->push_back(ParamSlot{slot_off, param_value_index(new_raw)});
        }
    }

    return dst_off;
}

#define LOGOS_MAP_OPS(Name, K) \
    const TypeOps k_map_##Name##_ops = { \
        type_hash::Map##Name, \
        s_map_k_anyval<K>, \
        nullptr, \
        nullptr, \
        c_map_k_anyval<K>, \
    }

LOGOS_MAP_OPS(I32AnyVal, int32_t);
LOGOS_MAP_OPS(U32AnyVal, uint32_t);
LOGOS_MAP_OPS(I64AnyVal, int64_t);
LOGOS_MAP_OPS(U64AnyVal, uint64_t);

#undef LOGOS_MAP_OPS

} // namespace

HERMES_REGISTER_TYPE(k_map_I32AnyVal_ops);
HERMES_REGISTER_TYPE(k_map_U32AnyVal_ops);
HERMES_REGISTER_TYPE(k_map_I64AnyVal_ops);
HERMES_REGISTER_TYPE(k_map_U64AnyVal_ops);

// Link-time anchor: referenced from type_ops.cpp so the static archive
// member is always pulled in (otherwise HERMES_REGISTER_TYPE's linker-
// section entry can be dropped by the archive-selection step).
void hermes_map_i32_anyval_anchor() noexcept {}

} // namespace logos::hermes
