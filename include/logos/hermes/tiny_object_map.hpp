// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <cstring>
#include <bit>
#include <new>

#include <logos/hermes/arena.hpp>
#include <logos/hermes/relative_ptr.hpp>
#include <logos/hermes/any_val.hpp>
#include <logos/hermes/type_registry.hpp>

namespace logos::hermes {

// TinyObjectMap: bitmap-indexed sparse map with uint8_t keys (0..51)
// and AnyVal values. O(1) lookup via popcount.
//
// All read/write methods take `uint8_t* base` — the segment base address.
// AnyVal values in pointer mode store segment-relative offsets, so they
// can be freely copied without relocation.
class TinyObjectMap {
public:
    static constexpr uint8_t  MAX_KEYS     = 52;
    static constexpr uint64_t BITMAP_MASK  = (1ULL << 52) - 1;

    uint8_t size() const     { return static_cast<uint8_t>((header_ >> 58) & 0x3F); }
    uint8_t capacity() const { return static_cast<uint8_t>((header_ >> 52) & 0x3F); }
    uint64_t bitmap() const  { return header_ & BITMAP_MASK; }

    bool has_key(uint8_t key) const {
        if (key >= MAX_KEYS) return false;
        return (header_ & (1ULL << key)) != 0;
    }

    AnyVal get(uint8_t key, uint8_t* base) const {
        if (!has_key(key)) return AnyVal{};
        return values(base)[index_of(key)];
    }

    AnyVal* slot(uint8_t key, uint8_t* base) {
        if (!has_key(key)) return nullptr;
        return &values(base)[index_of(key)];
    }

    const AnyVal* slot(uint8_t key, const uint8_t* base) const {
        if (!has_key(key)) return nullptr;
        return &const_cast<TinyObjectMap*>(this)->values(const_cast<uint8_t*>(base))[index_of(key)];
    }

    void put(uint8_t key, AnyVal value, Arena& arena) {
        if (key >= MAX_KEYS) return;
        uint8_t* base = arena.head().data();

        if (has_key(key)) {
            values(base)[index_of(key)] = value;
            return;
        }

        uint8_t sz = size();
        uint8_t cap = capacity();

        if (sz >= cap) {
            grow(arena, cap == 0 ? 4 : cap * 2);
            base = arena.head().data(); // re-derive after potential realloc
            cap = capacity();
        }

        uint8_t pos = index_of(key);
        AnyVal* vals = values(base);
        for (uint8_t i = sz; i > pos; --i) {
            vals[i] = vals[i - 1];
        }
        vals[pos] = value;

        header_ = (header_ & BITMAP_MASK) | (1ULL << key);
        header_ |= (static_cast<uint64_t>(cap) << 52);
        header_ |= (static_cast<uint64_t>(sz + 1) << 58);
    }

    bool remove(uint8_t key, uint8_t* base) {
        if (!has_key(key)) return false;

        uint8_t pos = index_of(key);
        uint8_t sz = size();
        uint8_t cap = capacity();

        AnyVal* vals = values(base);
        for (uint8_t i = pos; i + 1 < sz; ++i) {
            vals[i] = vals[i + 1];
        }
        vals[sz - 1] = AnyVal{};

        uint64_t bm = bitmap() & ~(1ULL << key);
        header_ = bm
                | (static_cast<uint64_t>(cap) << 52)
                | (static_cast<uint64_t>(sz - 1) << 58);
        return true;
    }

    static TinyObjectMap* create(Arena& arena, uint8_t initial_capacity = 4) {
        TypeTag tag(type_hash::Hermes, TagDescriptor::Map);
        void* mem = arena.allocate(sizeof(TinyObjectMap), alignof(TinyObjectMap), tag).get();
        auto* map = new (mem) TinyObjectMap();

        if (initial_capacity > 0) {
            map->grow(arena, initial_capacity);
        }
        return map;
    }

private:
    uint64_t header_ = 0;
    RelativePtr<AnyVal> data_;

    uint8_t index_of(uint8_t key) const {
        uint64_t mask_below = (1ULL << key) - 1;
        return static_cast<uint8_t>(std::popcount(bitmap() & mask_below));
    }

    AnyVal* values(uint8_t* base) const {
        return data_.get(base);
    }

    void grow(Arena& arena, uint8_t new_cap) {
        if (new_cap > MAX_KEYS) new_cap = MAX_KEYS;

        void* new_mem = arena.allocate_raw(new_cap * sizeof(AnyVal), alignof(AnyVal)).get();
        auto* new_vals = static_cast<AnyVal*>(new_mem);
        for (uint8_t i = 0; i < new_cap; ++i) new_vals[i] = AnyVal{};

        // Segment-relative: just copy. No relocation needed!
        uint8_t sz = size();
        uint8_t* base = arena.head().data();
        if (sz > 0 && !data_.is_null()) {
            AnyVal* old_vals = values(base);
            std::memcpy(new_vals, old_vals, sz * sizeof(AnyVal));
        }

        data_.set(new_vals, base);
        uint64_t bm = bitmap();
        header_ = bm
                | (static_cast<uint64_t>(new_cap) << 52)
                | (static_cast<uint64_t>(sz) << 58);
    }
};

// header_(8) + data_(4) + padding(4) = 16 with default arena_offset_t=uint32_t
static_assert(sizeof(TinyObjectMap) == 16);

} // namespace logos::hermes
