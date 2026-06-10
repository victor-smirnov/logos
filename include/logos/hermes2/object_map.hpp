// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <new>
#include <string_view>

#include <logos/hermes2/arena.hpp>
#include <logos/hermes2/relative_ptr.hpp>
#include <logos/hermes2/any_val.hpp>
#include <logos/hermes2/arena_string.hpp>
#include <logos/hermes2/fnv_hash.hpp>
#include <logos/hermes2/type_codes.hpp>
#include <logos/core/expected.hpp>

namespace logos::hermes2 {

// An entry of ObjectMap — two AT-REST AnyVal words (key, val), 16 bytes. Matches
// the Logos stdlib HMapEntry (stdlib/lang/hermes2/hmap.logos). key.is_null() (the
// zero word) marks an empty slot — the buffer is zero-initialised and a real Ref's
// at-rest delta is never 0.
struct MapEntry {
    AnyVal key;   // Ref → interned ArenaString (or null = empty slot)
    AnyVal val;   // the stored value
};
static_assert(sizeof(MapEntry) == 16);

// ObjectMap — open-addressing hash map, string keys → AnyVal values (the JSON
// "object"). BYTE-IDENTICAL to the Logos HMap<HString, HVal>:
//   { count : i64, cap : i64, data : self-relative ptr to MapEntry[] }   (24 bytes)
//
// Linear probing + FNV-1a; keys are interned ArenaStrings in the same arena. Grows
// (append a fresh 2× buffer + rehash) at load factor > 0.75 — unlike Hermes1's
// fixed-capacity zone map. Reseated onto self-relative storage: no `base`; the
// header never moves; rehash/probe RE-ANCHOR each at-rest key/val (AnyVal
// assignment lowers, in-place resolve() reads).
class ObjectMap {
public:
    uint64_t size()     const noexcept { return static_cast<uint64_t>(count_); }
    uint64_t capacity() const noexcept { return static_cast<uint64_t>(cap_); }
    bool     empty()    const noexcept { return count_ == 0; }

    bool has(std::string_view key) const noexcept { return find_slot(key) != nullptr; }

    AnyVal get(std::string_view key) const noexcept {
        const MapEntry* e = find_slot(key);
        return e ? e->val : AnyVal{};
    }
    // CUT-OVER VESTIGIAL (base ignored) — lets logosc base-threading sites compile.
    AnyVal get(std::string_view key, const void*) const noexcept { return get(key); }

    // Visit every live entry as fn(std::string_view key, AnyVal val).
    template <typename F>
    void for_each(F&& fn) const {
        if (cap_ == 0) return;
        const MapEntry* buf = entries();
        for (int64_t i = 0; i < cap_; ++i)
            if (!buf[i].key.is_null()) fn(key_view(buf[i].key), buf[i].val);
    }

    // Insert or update. On a NEW key the string is interned into THIS arena.
    [[nodiscard]] logos::expected<void> put(std::string_view key, AnyVal value, Arena& arena) noexcept {
        if ((count_ + 1) * 4 > cap_ * 3) {                  // load > 0.75
            LOGOS_TRY_VOID(grow(arena, cap_ == 0 ? 8 : cap_ * 2));
        }
        uint64_t mask = static_cast<uint64_t>(cap_) - 1;
        uint64_t h    = fnv1a_hash(key);
        MapEntry* buf = entries();
        for (uint64_t slot = h & mask; ; slot = (slot + 1) & mask) {
            MapEntry* e = &buf[slot];
            if (e->key.is_null()) {                         // empty → insert
                LOGOS_TRY(auto* ks, ArenaString::create(arena, key));
                e->key.set_ref(ks);                         // lowers to self-relative
                e->val = value;
                ++count_;
                return {};
            }
            if (key_view(e->key) == key) { e->val = value; return {}; }   // update
        }
    }

    [[nodiscard]] static logos::expected<ObjectMap*>
    create(Arena& arena, uint64_t initial_capacity = 8) noexcept {
        TypeTag tag(tc::MAP);
        LOGOS_TRY(auto* mem, arena.allocate(sizeof(ObjectMap), alignof(ObjectMap), tag));
        auto* m = new (mem) ObjectMap();
        LOGOS_TRY_VOID(m->grow(arena, round_pow2(initial_capacity)));
        return m;
    }

private:
    int64_t               count_ = 0;
    int64_t               cap_   = 0;
    RelativePtr<MapEntry> data_;

    MapEntry* entries() const noexcept { return data_.get(); }

    // Byte view of an interned key (resolve the at-rest Ref in place → ArenaString).
    static std::string_view key_view(const AnyVal& k) noexcept {
        return reinterpret_cast<const ArenaString*>(k.resolve())->view();
    }

    const MapEntry* find_slot(std::string_view key) const noexcept {
        if (cap_ == 0) return nullptr;
        uint64_t mask = static_cast<uint64_t>(cap_) - 1;
        uint64_t h    = fnv1a_hash(key);
        const MapEntry* buf = entries();
        for (uint64_t slot = h & mask; ; slot = (slot + 1) & mask) {
            const MapEntry* e = &buf[slot];
            if (e->key.is_null()) return nullptr;
            if (key_view(e->key) == key) return e;
        }
    }

    static uint64_t round_pow2(uint64_t n) noexcept {
        uint64_t c = 8;
        while (c < n) c *= 2;
        return c;
    }

    logos::expected<void> grow(Arena& arena, uint64_t new_cap) noexcept {
        LOGOS_TRY(auto* mem, arena.allocate_raw(new_cap * sizeof(MapEntry), alignof(MapEntry)));
        auto* nbuf = static_cast<MapEntry*>(mem);
        for (uint64_t i = 0; i < new_cap; ++i) new (&nbuf[i]) MapEntry();
        // Rehash every live entry into the fresh buffer (re-anchoring key + val).
        uint64_t mask = new_cap - 1;
        MapEntry* obuf = entries();
        for (int64_t i = 0; i < cap_; ++i) {
            if (obuf && !obuf[i].key.is_null()) {
                std::string_view k = key_view(obuf[i].key);
                uint64_t h = fnv1a_hash(k);
                uint64_t slot = h & mask;
                while (!nbuf[slot].key.is_null()) slot = (slot + 1) & mask;
                nbuf[slot].key = obuf[i].key;   // assignment re-anchors at the new slot
                nbuf[slot].val = obuf[i].val;
            }
        }
        data_ = nbuf;
        cap_  = static_cast<int64_t>(new_cap);
        return {};
    }
};

// count_(8) + cap_(8) + data_(8, self-relative i64) = 24 (matches HMap<HString,HVal>)
static_assert(sizeof(ObjectMap) == 24);

} // namespace logos::hermes2
