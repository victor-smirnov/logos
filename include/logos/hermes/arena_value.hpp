// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

#include <logos/hermes/arena.hpp>
#include <logos/hermes/any_val.hpp>
#include <logos/hermes/type_registry.hpp>
#include <logos/core/expected.hpp>

namespace logos::hermes {

// Allocate a fixed-size primitive value in the arena and return a pointer to it.
// Works for any type with TypeTraits specialization that has fixed_size == true.
//
// Usage:
//   int32_t* p = arena_put<int32_t>(arena, 42);
//   double*  d = arena_put<double>(arena, 3.14);
template <typename T>
    requires (TypeTraits<T>::fixed_size && std::is_trivially_copyable_v<T>)
[[nodiscard]] logos::expected<T*> arena_put(Arena& arena, T value) noexcept {
    TypeTag tag = type_tag_for<T>();
    LOGOS_TRY(void* mem, arena.allocate(sizeof(T), alignof(T) < 2 ? 2 : alignof(T), tag));
    std::memcpy(mem, &value, sizeof(T));
    return static_cast<T*>(mem);
}

// Allocate `value` in the arena and return a pointer-mode AnyVal that points
// to it. The canonical helper for non-embeddable scalars (f32/f64/i64/u64,
// etc.) under the 4-byte AnyVal layout: value mode carries only 24 bits so
// anything larger (or just `float`, per the wire spec) must live in the
// arena with AnyVal holding a segment-relative offset.
template <typename T>
    requires (TypeTraits<T>::fixed_size && std::is_trivially_copyable_v<T>)
[[nodiscard]] logos::expected<AnyVal> anyval_put(Arena& arena, T value) noexcept {
    LOGOS_TRY(T* p, arena_put<T>(arena, value));
    const uint8_t* base = arena.head().data();
    auto off = static_cast<uint32_t>(reinterpret_cast<const uint8_t*>(p) - base);
    return AnyVal::from_offset(arena_offset_t{off});
}

// Read back a fixed-size value from an arena pointer.
// This is trivial for fixed-size types — just dereference — but provides
// symmetry with arena_put and a place to add validation later.
template <typename T>
    requires (TypeTraits<T>::fixed_size && std::is_trivially_copyable_v<T>)
T arena_get(const T* ptr) noexcept {
    T value;
    std::memcpy(&value, ptr, sizeof(T));
    return value;
}

// UID types: fixed-size byte arrays, not C++ primitives.
// Stored in the arena as raw bytes with their TypeTag.

struct Uid64 {
    uint64_t value;
};

struct Uid128 {
    uint8_t bytes[16];
};

struct Uid256 {
    uint8_t bytes[32];
};

static_assert(sizeof(Uid64) == 8);
static_assert(sizeof(Uid128) == 16);
static_assert(sizeof(Uid256) == 32);

template <> struct TypeTraits<Uid64> {
    static constexpr uint64_t hash = type_hash::Uid64;
    static constexpr bool fixed_size = true;
    static constexpr bool embeddable = false;
    static constexpr TagDescriptor descriptor = TagDescriptor::Data;
};

template <> struct TypeTraits<Uid128> {
    static constexpr uint64_t hash = type_hash::Uid128;
    static constexpr bool fixed_size = true;
    static constexpr bool embeddable = false;
    static constexpr TagDescriptor descriptor = TagDescriptor::Data;
};

template <> struct TypeTraits<Uid256> {
    static constexpr uint64_t hash = type_hash::Uid256;
    static constexpr bool fixed_size = true;
    static constexpr bool embeddable = false;
    static constexpr TagDescriptor descriptor = TagDescriptor::Data;
};

} // namespace logos::hermes
