// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>

#include <logos/hermes/arena.hpp>
#include <logos/hermes/any_val.hpp>
#include <logos/hermes/relative_ptr.hpp>
#include <logos/hermes/type_registry.hpp>
#include <logos/core/expected.hpp>

namespace logos::hermes {

// Map<K, V>: dense dual-buffer map with linear lookup, byte-compatible
// with stdlib/hermes/map.logos `Map<i32, AnyVal>` and future partial
// specialisations keyed by a trivially-copyable K.
//
// Header layout (16 bytes, 4-byte aligned):
//   uint32_t size_
//   uint32_t capacity_
//   RelativePtr<K> keys_   (u32)
//   RelativePtr<V> vals_   (u32)
//
// Two parallel buffers (keys[], vals[]) live elsewhere in the arena, each
// `capacity` entries long.
//
// Concrete Logos type_code binding:
//   Map<int32_t, AnyVal> → 105
template <typename K, typename V>
class TypedMap {
    static_assert(std::is_trivially_copyable_v<K>,
                  "Map<K,V> requires trivially-copyable K");
    static_assert(std::is_trivially_copyable_v<V>,
                  "Map<K,V> requires trivially-copyable V (AnyVal is fine)");

public:
    uint32_t size()     const noexcept { return size_; }
    uint32_t capacity() const noexcept { return capacity_; }
    bool     empty()    const noexcept { return size_ == 0; }

    K* keys(uint8_t* base) const noexcept { return keys_.get(base); }
    V* vals(uint8_t* base) const noexcept { return vals_.get(base); }

    V* get_slot(K key, uint8_t* base) noexcept {
        K* ks = keys(base);
        V* vs = vals(base);
        for (uint32_t i = 0; i < size_; ++i) {
            if (ks[i] == key) return &vs[i];
        }
        return nullptr;
    }

    const V* get_slot(K key, const uint8_t* base) const noexcept {
        const K* ks = keys_.get(base);
        const V* vs = vals_.get(base);
        for (uint32_t i = 0; i < size_; ++i) {
            if (ks[i] == key) return &vs[i];
        }
        return nullptr;
    }

    // Insert-or-update. Returns false if size == capacity and the key was
    // absent — matches stdlib/hermes/map.logos::set semantics (silent drop).
    // Does not grow the underlying buffers; capacity is fixed at create().
    bool put(K key, V val, uint8_t* base) noexcept {
        K* ks = keys_.get(base);
        V* vs = vals_.get(base);
        for (uint32_t i = 0; i < size_; ++i) {
            if (ks[i] == key) { vs[i] = val; return true; }
        }
        if (size_ >= capacity_) return false;
        ks[size_] = key;
        vs[size_] = val;
        ++size_;
        return true;
    }

    // Allocate header + keys[] + vals[] in `arena`. All three allocations
    // happen up-front; capacity is fixed for the lifetime of the map
    // (no grow path yet — matches Logos Map<i32,AnyVal>::init).
    [[nodiscard]] static logos::expected<TypedMap*> create(
            Arena& arena, uint32_t initial_capacity) noexcept {
        TypeTag tag(type_code_for(), TagDescriptor::Map);
        LOGOS_TRY(auto* mem_void,
            arena.allocate(sizeof(TypedMap), alignof(TypedMap), tag));
        auto* self = new (mem_void) TypedMap();
        ptrdiff_t self_off = reinterpret_cast<uint8_t*>(self) - arena.head().data();

        if (initial_capacity == 0) {
            return reinterpret_cast<TypedMap*>(arena.head().data() + self_off);
        }

        // Allocate keys[] — may grow arena.
        LOGOS_TRY(auto* keys_void,
            arena.allocate_raw(initial_capacity * sizeof(K), alignof(K)));
        std::memset(keys_void, 0, initial_capacity * sizeof(K));
        ptrdiff_t keys_off =
            reinterpret_cast<uint8_t*>(keys_void) - arena.head().data();

        // Allocate vals[] — may grow arena again; re-derive base each time.
        LOGOS_TRY(auto* vals_void,
            arena.allocate_raw(initial_capacity * sizeof(V), alignof(V)));
        std::memset(vals_void, 0, initial_capacity * sizeof(V));
        ptrdiff_t vals_off =
            reinterpret_cast<uint8_t*>(vals_void) - arena.head().data();

        // Now write the header using the final base.
        uint8_t* base = arena.head().data();
        auto* h = reinterpret_cast<TypedMap*>(base + self_off);
        h->size_     = 0;
        h->capacity_ = initial_capacity;
        h->keys_.set_offset(arena_offset_t(static_cast<uint32_t>(keys_off)));
        h->vals_.set_offset(arena_offset_t(static_cast<uint32_t>(vals_off)));
        return h;
    }

    static constexpr uint64_t type_code_for() noexcept {
        if constexpr (std::is_same_v<V, AnyVal>) {
            if constexpr (std::is_same_v<K, int32_t>)  return type_hash::MapI32AnyVal;
            if constexpr (std::is_same_v<K, uint32_t>) return type_hash::MapU32AnyVal;
            if constexpr (std::is_same_v<K, int64_t>)  return type_hash::MapI64AnyVal;
            if constexpr (std::is_same_v<K, uint64_t>) return type_hash::MapU64AnyVal;
        }
        return 0;
    }

private:
    uint32_t       size_     = 0;
    uint32_t       capacity_ = 0;
    RelativePtr<K> keys_;
    RelativePtr<V> vals_;
};

static_assert(sizeof(TypedMap<int32_t,  AnyVal>) == 16);
static_assert(sizeof(TypedMap<uint32_t, AnyVal>) == 16);
static_assert(sizeof(TypedMap<int64_t,  AnyVal>) == 16);
static_assert(sizeof(TypedMap<uint64_t, AnyVal>) == 16);

using MapI32AnyVal = TypedMap<int32_t,  AnyVal>;
using MapU32AnyVal = TypedMap<uint32_t, AnyVal>;
using MapI64AnyVal = TypedMap<int64_t,  AnyVal>;
using MapU64AnyVal = TypedMap<uint64_t, AnyVal>;

} // namespace logos::hermes
