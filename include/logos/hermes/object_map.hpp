// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>
#include <new>

#include <logos/hermes/arena.hpp>
#include <logos/hermes/relative_ptr.hpp>
#include <logos/hermes/any_val.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/fnv_hash.hpp>
#include <logos/hermes/type_registry.hpp>
#include <logos/core/expected.hpp>

namespace logos::hermes {

// MapEntry: one slot in the hash table (8 bytes).
//   key_off: zone-relative offset to ArenaString key (0 = empty slot).
//   val:     32-bit AnyVal.raw payload.
// Empty-slot sentinel key_off == 0 exploits the fact that offset 0 is the
// DocumentHeader and never a data object.
struct MapEntry {
    RelativePtr<ArenaString> key_off;
    uint32_t                 val;
};
static_assert(sizeof(MapEntry) == 8);

// ObjectMap: open-addressing hash map with linear probing, ArenaString keys
// and AnyVal values. 16-byte header, byte-for-byte compatible with
// stdlib/hermes/objectmap.logos Map<Varchar, AnyVal>.
class ObjectMap {
public:
    uint64_t size()     const noexcept { return count_; }
    bool     empty()    const noexcept { return count_ == 0; }
    uint64_t capacity() const noexcept { return capacity_; }

    // --- Lookup ---

    AnyVal get(std::string_view key, uint8_t* base) const noexcept {
        const MapEntry* e = find_entry(key, base);
        if (!e) return AnyVal{};
        return AnyVal::from_raw(e->val);
    }

    bool has(std::string_view key, uint8_t* base) const noexcept {
        return find_entry(key, base) != nullptr;
    }

    AnyVal* get_slot(std::string_view key, uint8_t* base) noexcept {
        MapEntry* e = const_cast<MapEntry*>(find_entry(key, base));
        if (!e) return nullptr;
        // val is a uint32_t co-located where an AnyVal raw lives.
        return reinterpret_cast<AnyVal*>(&e->val);
    }

    const AnyVal* get_slot(std::string_view key, const uint8_t* base) const noexcept {
        return const_cast<ObjectMap*>(this)->get_slot(key, const_cast<uint8_t*>(base));
    }

    // --- Iteration ---

    template <typename Fn>
    void for_each(Fn fn, uint8_t* base) const noexcept {
        if (count_ == 0 || capacity_ == 0) return;
        MapEntry* ents = const_cast<MapEntry*>(entries(base));
        for (uint32_t i = 0; i < capacity_; ++i) {
            if (ents[i].key_off.is_null()) continue;
            ArenaString* k = ents[i].key_off.get(base);
            fn(k, reinterpret_cast<AnyVal*>(&ents[i].val));
        }
    }

    // --- Mutation ---

    [[nodiscard]] logos::expected<void> put(std::string_view key, AnyVal value, Arena& arena) noexcept {
        // Any arena allocation below may move the backing chunk (for
        // GrowableSingleChunk), so capture our offset up front and re-resolve
        // `this` from (base + self_off) before any mutation of member fields.
        ptrdiff_t self_off = reinterpret_cast<uint8_t*>(this) - arena.head().data();

        if (capacity_ == 0) {
            LOGOS_TRY_VOID(init_entries(arena, 8));
        }
        {
            auto* self = reinterpret_cast<ObjectMap*>(arena.head().data() + self_off);
            // Load factor 0.75.
            if ((self->count_ + 1) * 4 >= self->capacity_ * 3) {
                LOGOS_TRY_VOID(self->rehash(arena, self->capacity_ * 2));
            }
        }

        uint8_t* base = arena.head().data();
        auto* self = reinterpret_cast<ObjectMap*>(base + self_off);

        // Check if key exists; if so, update in place and return.
        if (MapEntry* existing = const_cast<MapEntry*>(self->find_entry(key, base))) {
            existing->val = value.raw();
            return {};
        }

        // Pre-allocate the key string before locating the insert slot,
        // since allocation may move the arena buffer.
        LOGOS_TRY(auto* arena_key, ArenaString::create(arena, key));
        base = arena.head().data();
        self = reinterpret_cast<ObjectMap*>(base + self_off);

        MapEntry* ents = self->entries(base);
        uint64_t h = fnv1a_hash(key);
        uint64_t slot = h & (self->capacity_ - 1);
        for (uint32_t probed = 0; probed < self->capacity_; ++probed) {
            if (ents[slot].key_off.is_null()) {
                ents[slot].key_off.set(arena_key, base);
                ents[slot].val = value.raw();
                ++self->count_;
                return {};
            }
            ++slot;
            if (slot >= self->capacity_) slot = 0;
        }
        // Table full after rehash guard — shouldn't happen.
        return {};
    }

    bool remove(std::string_view key, Arena& arena) noexcept {
        uint8_t* base = arena.head().data();
        if (count_ == 0 || capacity_ == 0) return false;
        MapEntry* ents = entries(base);
        uint64_t h = fnv1a_hash(key);
        uint64_t slot = h & (capacity_ - 1);
        for (uint32_t probed = 0; probed < capacity_; ++probed) {
            if (ents[slot].key_off.is_null()) return false;
            const ArenaString* k = ents[slot].key_off.get(base);
            if (k && *k == key) {
                // Backshift subsequent entries in the probe chain.
                uint64_t i = slot;
                while (true) {
                    uint64_t j = (i + 1) & (capacity_ - 1);
                    if (ents[j].key_off.is_null()) break;
                    const ArenaString* kj = ents[j].key_off.get(base);
                    uint64_t hj = kj->hash();
                    uint64_t ideal = hj & (capacity_ - 1);
                    // Distance of j from its ideal slot.
                    uint64_t dist_j  = (j - ideal) & (capacity_ - 1);
                    uint64_t dist_ij = (j - i)     & (capacity_ - 1);
                    if (dist_j >= dist_ij) {
                        ents[i] = ents[j];
                        i = j;
                    } else {
                        break;
                    }
                }
                ents[i].key_off.clear();
                ents[i].val = 0;
                --count_;
                return true;
            }
            ++slot;
            if (slot >= capacity_) slot = 0;
        }
        return false;
    }

    // --- Factory ---

    [[nodiscard]] static logos::expected<ObjectMap*> create(Arena& arena, uint32_t initial_capacity = 8) noexcept {
        TypeTag tag(type_hash::ObjectMap, TagDescriptor::Map);
        LOGOS_TRY(auto* mem, arena.allocate(sizeof(ObjectMap), alignof(ObjectMap), tag));
        auto* map = new (mem) ObjectMap();
        ptrdiff_t map_off = reinterpret_cast<uint8_t*>(map) - arena.head().data();
        if (initial_capacity > 0) {
            // Round up to power of two.
            uint32_t cap = 1;
            while (cap < initial_capacity) cap <<= 1;
            LOGOS_TRY_VOID(map->init_entries(arena, cap));
        }
        return reinterpret_cast<ObjectMap*>(arena.head().data() + map_off);
    }

private:
    RelativePtr<MapEntry> entries_off_;
    uint32_t              capacity_ = 0;
    uint32_t              count_    = 0;
    [[maybe_unused]] uint32_t reserved_ = 0;

    MapEntry*       entries(uint8_t* base)       noexcept { return entries_off_.get(base); }
    const MapEntry* entries(const uint8_t* base) const noexcept {
        return entries_off_.get(const_cast<uint8_t*>(base));
    }

    const MapEntry* find_entry(std::string_view key, const uint8_t* base) const noexcept {
        if (count_ == 0 || capacity_ == 0) return nullptr;
        const MapEntry* ents = entries(base);
        uint64_t h = fnv1a_hash(key);
        uint64_t slot = h & (capacity_ - 1);
        for (uint32_t probed = 0; probed < capacity_; ++probed) {
            if (ents[slot].key_off.is_null()) return nullptr;
            const ArenaString* k = ents[slot].key_off.get(const_cast<uint8_t*>(base));
            if (k && *k == key) return &ents[slot];
            ++slot;
            if (slot >= capacity_) slot = 0;
        }
        return nullptr;
    }

    logos::expected<void> init_entries(Arena& arena, uint32_t cap) noexcept {
        ptrdiff_t self_off = reinterpret_cast<uint8_t*>(this) - arena.head().data();
        size_t alloc = static_cast<size_t>(cap) * sizeof(MapEntry);
        LOGOS_TRY(auto* mem_void, arena.allocate_raw(alloc, alignof(MapEntry)));
        auto* mem = static_cast<MapEntry*>(mem_void);
        std::memset(mem, 0, alloc);
        uint8_t* base = arena.head().data();
        auto* self = reinterpret_cast<ObjectMap*>(base + self_off);
        self->entries_off_.set(mem, base);
        self->capacity_ = cap;
        self->count_    = 0;
        return {};
    }

    logos::expected<void> rehash(Arena& arena, uint32_t new_cap) noexcept {
        // Snapshot before any allocation that may move the arena.
        ptrdiff_t self_off = reinterpret_cast<uint8_t*>(this) - arena.head().data();
        uint32_t old_cap = capacity_;
        RelativePtr<MapEntry> old_entries_off = entries_off_;

        LOGOS_TRY_VOID(init_entries(arena, new_cap));
        uint8_t* base = arena.head().data();
        auto* self = reinterpret_cast<ObjectMap*>(base + self_off);

        if (old_cap == 0) return {};

        MapEntry* old_ents = old_entries_off.get(base);
        MapEntry* new_ents = self->entries(base);

        for (uint32_t i = 0; i < old_cap; ++i) {
            if (old_ents[i].key_off.is_null()) continue;
            const ArenaString* k = old_ents[i].key_off.get(base);
            uint64_t h = k->hash();
            uint64_t slot = h & (new_cap - 1);
            while (!new_ents[slot].key_off.is_null()) {
                ++slot;
                if (slot >= new_cap) slot = 0;
            }
            new_ents[slot] = old_ents[i];
            ++self->count_;
        }
        return {};
    }
};

// entries_off(4) + capacity(4) + count(4) + reserved(4) = 16
static_assert(sizeof(ObjectMap) == 16);

} // namespace logos::hermes
