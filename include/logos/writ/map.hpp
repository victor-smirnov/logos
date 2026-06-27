// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <new>
#include <type_traits>

#include <logos/writ/arena.hpp>
#include <logos/writ/relative_ptr.hpp>
#include <logos/writ/any_val.hpp>
#include <logos/writ/type_codes.hpp>
#include <logos/core/expected.hpp>

namespace logos::writ {

// TypedMap<K> — a dense, FIXED-capacity map of a scalar integer key K (i32/u32/
// i64/u64) → AnyVal. BYTE-IDENTICAL to the Logos stdlib WMap<K, WAny>
// (stdlib/lang/writ/wmap.logos "dense spec"):
//   { size_ : i64, cap_ : i64, keys_ : self-rel ptr to K[], vals_ : self-rel ptr to AnyVal[] }
//   (32 bytes)
//
// Two parallel buffers — plain K keys (position-independent) + at-rest AnyVal vals
// (self-relative) — with O(n) linear lookup (the map is small). Fixed capacity →
// `put` never allocates after create (a new key into a full map is a no-op).
template <typename K>
class TypedMap {
    static_assert(std::is_integral_v<K>, "TypedMap<K> requires an integer key");

public:
    uint64_t size()     const noexcept { return static_cast<uint64_t>(size_); }
    uint64_t capacity() const noexcept { return static_cast<uint64_t>(cap_); }
    bool     empty()    const noexcept { return size_ == 0; }

    bool contains(K key) const noexcept {
        const K* ks = keys_.get();
        for (int64_t i = 0; i < size_; ++i) if (ks[i] == key) return true;
        return false;
    }

    AnyVal get(K key) const noexcept {
        const K* ks = keys_.get();
        AnyVal* vs = vals_.get();
        for (int64_t i = 0; i < size_; ++i) if (ks[i] == key) return vs[i];
        return AnyVal{};
    }

    // Visit every entry as fn(K key, AnyVal val).
    template <typename F>
    void for_each(F&& fn) const {
        const K* ks = keys_.get();
        AnyVal* vs = vals_.get();
        for (int64_t i = 0; i < size_; ++i) fn(ks[i], vs[i]);
    }

    // Insert or update. A new key into a full map is a no-op (fixed capacity).
    void put(K key, AnyVal value) noexcept {
        K* ks = keys_.get();
        AnyVal* vs = vals_.get();
        for (int64_t i = 0; i < size_; ++i) {
            if (ks[i] == key) { vs[i] = value; return; }   // update (assignment re-anchors)
        }
        if (size_ >= cap_) return;                         // full
        ks[size_] = key;
        vs[size_] = value;
        ++size_;
    }

    static constexpr uint64_t type_code_for() noexcept {
        if constexpr (std::is_same_v<K, int32_t>)  return tc::MAP_I32;
        if constexpr (std::is_same_v<K, uint32_t>) return tc::MAP_U32;
        if constexpr (std::is_same_v<K, int64_t>)  return tc::MAP_I64;
        if constexpr (std::is_same_v<K, uint64_t>) return tc::MAP_U64;
        return 0;
    }

    [[nodiscard]] static logos::expected<TypedMap*>
    create(Arena& arena, uint64_t cap) noexcept {
        TypeTag tag(type_code_for());
        LOGOS_TRY(auto* mem, arena.allocate(sizeof(TypedMap), alignof(TypedMap), tag));
        auto* m = new (mem) TypedMap();
        LOGOS_TRY(auto* kmem, arena.allocate_raw(cap * sizeof(K), alignof(K)));
        LOGOS_TRY(auto* vmem, arena.allocate_raw(cap * sizeof(AnyVal), alignof(AnyVal)));
        auto* ks = static_cast<K*>(kmem);
        auto* vs = static_cast<AnyVal*>(vmem);
        for (uint64_t i = 0; i < cap; ++i) { ks[i] = K{}; new (&vs[i]) AnyVal(); }
        m->size_ = 0;
        m->cap_  = static_cast<int64_t>(cap);
        m->keys_ = ks;
        m->vals_ = vs;
        return m;
    }

private:
    int64_t             size_ = 0;
    int64_t             cap_  = 0;
    RelativePtr<K>      keys_;
    RelativePtr<AnyVal> vals_;
};

// size_(8) + cap_(8) + keys_(8) + vals_(8) = 32 (matches WMap<K,WAny>)
static_assert(sizeof(TypedMap<int32_t>) == 32);
static_assert(sizeof(TypedMap<uint64_t>) == 32);

using MapI32 = TypedMap<int32_t>;
using MapU32 = TypedMap<uint32_t>;
using MapI64 = TypedMap<int64_t>;
using MapU64 = TypedMap<uint64_t>;

} // namespace logos::writ
