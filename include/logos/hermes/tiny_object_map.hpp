// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov

#pragma once

#include <cstdint>
#include <cstring>
#include <bit>
#include <new>

#include <logos/hermes/arena.hpp>
#include <logos/hermes/relative_ptr.hpp>
#include <logos/hermes/any_val.hpp>
#include <logos/hermes/type_registry.hpp>
#include <logos/core/expected.hpp>

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

    uint8_t  size()     const noexcept { return static_cast<uint8_t>((header_ >> 58) & 0x3F); }
    uint8_t  capacity() const noexcept { return static_cast<uint8_t>((header_ >> 52) & 0x3F); }
    uint64_t bitmap()   const noexcept { return header_ & BITMAP_MASK; }

    bool has_key(uint8_t key) const noexcept {
        if (key >= MAX_KEYS) return false;
        return (header_ & (1ULL << key)) != 0;
    }

    AnyVal get(uint8_t key, uint8_t* base) const noexcept {
        if (!has_key(key)) return AnyVal{};
        return values(base)[index_of(key)];
    }

    AnyVal* slot(uint8_t key, uint8_t* base) noexcept {
        if (!has_key(key)) return nullptr;
        return &values(base)[index_of(key)];
    }

    const AnyVal* slot(uint8_t key, const uint8_t* base) const noexcept {
        if (!has_key(key)) return nullptr;
        return &const_cast<TinyObjectMap*>(this)->values(
                    const_cast<uint8_t*>(base))[index_of(key)];
    }

    // Returns expected<void>; fails only on OOM (arena grow needed).
    // Silently ignores keys >= MAX_KEYS (no-op success).
    [[nodiscard]] logos::expected<void> put(
            uint8_t key, AnyVal value, Arena& arena) noexcept {
        if (key >= MAX_KEYS) return {};
        uint8_t* base = arena.head().data();

        if (has_key(key)) {
            values(base)[index_of(key)] = value;
            return {};
        }

        uint8_t sz  = size();
        uint8_t cap = capacity();

        TinyObjectMap* self = this;
        if (sz >= cap) {
            // grow() may realloc the arena buffer, making `this` stale.
            ptrdiff_t self_off = reinterpret_cast<uint8_t*>(this) - arena.head().data();
            auto r = grow(arena, cap == 0 ? 4 : cap * 2);
            if (!r) return r;
            base = arena.head().data(); // re-derive after potential realloc
            self = reinterpret_cast<TinyObjectMap*>(base + self_off);
            cap  = self->capacity();
            sz   = self->size();
        }

        uint8_t  pos  = self->index_of(key);
        AnyVal*  vals = self->values(base);
        for (uint8_t i = sz; i > pos; --i) vals[i] = vals[i - 1];
        vals[pos] = value;

        self->header_ = (self->header_ & BITMAP_MASK) | (1ULL << key);
        self->header_ |= (static_cast<uint64_t>(cap)      << 52);
        self->header_ |= (static_cast<uint64_t>(sz + 1)   << 58);
        return {};
    }

    bool remove(uint8_t key, uint8_t* base) noexcept {
        if (!has_key(key)) return false;

        uint8_t pos = index_of(key);
        uint8_t sz  = size();
        uint8_t cap = capacity();

        AnyVal* vals = values(base);
        for (uint8_t i = pos; i + 1 < sz; ++i) vals[i] = vals[i + 1];
        vals[sz - 1] = AnyVal{};

        uint64_t bm = bitmap() & ~(1ULL << key);
        header_ = bm
                | (static_cast<uint64_t>(cap)    << 52)
                | (static_cast<uint64_t>(sz - 1) << 58);
        return true;
    }

    [[nodiscard]] static logos::expected<TinyObjectMap*> create(
            Arena& arena, uint8_t initial_capacity = 4) noexcept {
        TypeTag tag(type_hash::Hermes, TagDescriptor::Map);
        auto mem_exp = arena.allocate(sizeof(TinyObjectMap), alignof(TinyObjectMap), tag);
        if (!mem_exp) return std::unexpected(std::move(mem_exp.error()));

        auto* map = new (*mem_exp) TinyObjectMap();
        // Save offset after allocation so we can recompute if grow() reallocs the buffer.
        ptrdiff_t map_off = reinterpret_cast<uint8_t*>(map) - arena.head().data();
        if (initial_capacity > 0) {
            auto r = map->grow(arena, initial_capacity);
            if (!r) return std::unexpected(std::move(r.error()));
        }
        return reinterpret_cast<TinyObjectMap*>(arena.head().data() + map_off);
    }

private:
    uint64_t header_ = 0;
    RelativePtr<AnyVal> data_;

    uint8_t index_of(uint8_t key) const noexcept {
        uint64_t mask_below = (1ULL << key) - 1;
        return static_cast<uint8_t>(std::popcount(bitmap() & mask_below));
    }

    AnyVal* values(uint8_t* base) const noexcept { return data_.get(base); }

    [[nodiscard]] logos::expected<void> grow(Arena& arena, uint8_t new_cap) noexcept {
        if (new_cap > MAX_KEYS) new_cap = MAX_KEYS;

        // Save our offset from the arena base BEFORE allocating.
        // GrowableSingleChunk may realloc its buffer during allocate_raw,
        // making `this` a dangling pointer.  Recompute self from new base after.
        uint8_t* base_before = arena.head().data();
        ptrdiff_t self_off = reinterpret_cast<uint8_t*>(this) - base_before;

        auto mem_exp = arena.allocate_raw(new_cap * sizeof(AnyVal), alignof(AnyVal));
        if (!mem_exp) return std::unexpected(std::move(mem_exp.error()));

        auto* new_vals = static_cast<AnyVal*>(*mem_exp);
        for (uint8_t i = 0; i < new_cap; ++i) new_vals[i] = AnyVal{};

        uint8_t* base = arena.head().data();
        // Recompute self — for MultiChunk base never moves so self == this;
        // for GrowableSingleChunk after realloc, self points into the new buffer.
        auto* self = reinterpret_cast<TinyObjectMap*>(base + self_off);

        uint8_t sz = self->size();
        if (sz > 0 && !self->data_.is_null()) {
            std::memcpy(new_vals, self->values(base), sz * sizeof(AnyVal));
        }

        self->data_.set(new_vals, base);
        self->header_ = self->bitmap()
                | (static_cast<uint64_t>(new_cap) << 52)
                | (static_cast<uint64_t>(sz)       << 58);
        return {};
    }
};

// header_(8) + data_(4) + padding(4) = 16 with default arena_offset_t=uint32_t
static_assert(sizeof(TinyObjectMap) == 16);

} // namespace logos::hermes
