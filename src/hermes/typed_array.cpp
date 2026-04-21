// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// typed_array.cpp — TypeOps registration for TypedArray<int32_t> (tc=104)
// and TypedArray<uint64_t> (tc=108). Mirrors stdlib/hermes/array.logos
// Array<T> clone/stringify handlers.

#include <logos/hermes/typed_array.hpp>
#include <logos/hermes/clone.hpp>
#include <logos/hermes/type_ops.hpp>
#include <logos/hermes/type_tag.hpp>
#include <logos/hermes/type_registry.hpp>

#include <cstring>
#include <string>

namespace logos::hermes {

namespace {

static inline uint32_t dst_offset_of(const void* p, Arena& dst) noexcept {
    return static_cast<uint32_t>(
        static_cast<const uint8_t*>(p) - dst.head().data());
}

// Stringify: [v, v, v]
template <typename T>
static logos::expected<void>
s_typed_array(const uint8_t* o, StringifyCtx* c) noexcept {
    auto* a = reinterpret_cast<const TypedArray<T>*>(o);
    *c->out += '[';
    uint64_t n = a->size();
    for (uint64_t i = 0; i < n; ++i) {
        if (i > 0) *c->out += ',';
        T v = const_cast<TypedArray<T>*>(a)->get(i, const_cast<uint8_t*>(c->base));
        if constexpr (std::is_floating_point_v<T>) {
            *c->out += std::to_string(static_cast<double>(v));
        } else if constexpr (std::is_signed_v<T>) {
            *c->out += std::to_string(static_cast<int64_t>(v));
        } else {
            *c->out += std::to_string(static_cast<uint64_t>(v));
        }
    }
    *c->out += ']';
    return {};
}

// Clone: copy header + elements; no AnyVal recursion needed (elements are
// trivially-copyable scalars, not tagged objects).
template <typename T>
static logos::expected<uint32_t>
c_typed_array(const uint8_t* o, CloneCtx* c) noexcept {
    auto* src = reinterpret_cast<const TypedArray<T>*>(o);
    uint32_t src_off = static_cast<uint32_t>(o - c->base_src);
    uint64_t n = src->size();

    // Snapshot src data pointer before any dst alloc (src arena is separate,
    // but keep symmetry with the AnyVal-container handlers).
    const T* src_data = const_cast<TypedArray<T>*>(src)
        ->slot(0, const_cast<uint8_t*>(c->base_src));

    // Allocate dst header.
    constexpr uint64_t tc = TypedArray<T>::type_code_for();
    TypeTag tag(tc, TagDescriptor::Array);
    LOGOS_TRY(auto* hdr_void,
        c->dst->allocate(sizeof(TypedArray<T>), alignof(TypedArray<T>), tag));
    auto* hdr = new (hdr_void) TypedArray<T>();
    (void)hdr;
    uint32_t dst_off = dst_offset_of(hdr_void, *c->dst);
    c->map.emplace(src_off, dst_off);

    if (n == 0) {
        // Write size=capacity=0 header, no data buffer.
        auto* raw = reinterpret_cast<uint32_t*>(hdr_void);
        // Layout: u64 size, u64 capacity, u32 data_off, u32 pad.
        raw[0] = 0; raw[1] = 0; raw[2] = 0; raw[3] = 0;
        raw[4] = 0; raw[5] = 0;
        return dst_off;
    }

    // Allocate data buffer.
    LOGOS_TRY(auto* data_void,
        c->dst->allocate_raw(n * sizeof(T), alignof(T)));
    uint32_t data_off = static_cast<uint32_t>(
        static_cast<uint8_t*>(data_void) - c->dst->head().data());
    std::memcpy(data_void, src_data, n * sizeof(T));

    // Write header fields (bypass private access via raw layout):
    //   offsets: u64 size(0), u64 capacity(8), u32 data_off(16), u32 pad(20).
    uint8_t* base = c->dst->head().data();
    auto* dst_hdr_bytes = base + dst_off;
    std::memcpy(dst_hdr_bytes,      &n, sizeof(uint64_t));
    std::memcpy(dst_hdr_bytes + 8,  &n, sizeof(uint64_t));
    std::memcpy(dst_hdr_bytes + 16, &data_off, sizeof(uint32_t));
    uint32_t zero = 0;
    std::memcpy(dst_hdr_bytes + 20, &zero, sizeof(uint32_t));
    return dst_off;
}

#define LOGOS_ARRAY_OPS(Name, T) \
    const TypeOps k_array_##Name##_ops = { \
        type_hash::Array##Name, \
        s_typed_array<T>, \
        nullptr, \
        nullptr, \
        c_typed_array<T>, \
    }

LOGOS_ARRAY_OPS(I8,  int8_t);
LOGOS_ARRAY_OPS(U8,  uint8_t);
LOGOS_ARRAY_OPS(I16, int16_t);
LOGOS_ARRAY_OPS(U16, uint16_t);
LOGOS_ARRAY_OPS(I32, int32_t);
LOGOS_ARRAY_OPS(U32, uint32_t);
LOGOS_ARRAY_OPS(I64, int64_t);
LOGOS_ARRAY_OPS(U64, uint64_t);
LOGOS_ARRAY_OPS(F32, float);
LOGOS_ARRAY_OPS(F64, double);

#undef LOGOS_ARRAY_OPS

} // namespace

HERMES_REGISTER_TYPE(k_array_I8_ops);
HERMES_REGISTER_TYPE(k_array_U8_ops);
HERMES_REGISTER_TYPE(k_array_I16_ops);
HERMES_REGISTER_TYPE(k_array_U16_ops);
HERMES_REGISTER_TYPE(k_array_I32_ops);
HERMES_REGISTER_TYPE(k_array_U32_ops);
HERMES_REGISTER_TYPE(k_array_I64_ops);
HERMES_REGISTER_TYPE(k_array_U64_ops);
HERMES_REGISTER_TYPE(k_array_F32_ops);
HERMES_REGISTER_TYPE(k_array_F64_ops);

// Link-time anchor (referenced from type_ops.cpp).
void hermes_typed_array_anchor() noexcept {}

} // namespace logos::hermes
