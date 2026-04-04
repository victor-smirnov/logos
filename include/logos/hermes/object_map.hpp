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

// ObjectMap: string-keyed hash map with AnyVal values.
// All methods take segment base via Arena or explicit uint8_t* base.
class ObjectMap {
public:
    uint64_t size() const noexcept { return size_; }
    bool empty() const noexcept    { return size_ == 0; }
    uint64_t bucket_count() const noexcept { return 1ULL << bucket_capacity_; }

    // --- Lookup ---

    AnyVal get(std::string_view key, uint8_t* base) const noexcept {
        if (size_ == 0) return AnyVal{};

        uint64_t h = fnv1a_hash(key);
        uint64_t idx = h & (bucket_count() - 1);

        auto* bucket_ptrs = buckets_.get(base);
        if (bucket_ptrs[idx].is_null()) return AnyVal{};

        auto* bucket = bucket_ptrs[idx].get(base);
        uint32_t bsz = bucket_size(bucket);
        auto* keys = bucket_keys(bucket);

        for (uint32_t i = 0; i < bsz; ++i) {
            ArenaString* k = keys[i].get(base);
            if (k && *k == key) {
                return bucket_values(bucket, bucket_cap(bucket))[i];
            }
        }
        return AnyVal{};
    }

    bool has(std::string_view key, uint8_t* base) const noexcept {
        return get_slot(key, base) != nullptr;
    }

    AnyVal* get_slot(std::string_view key, uint8_t* base) noexcept {
        if (size_ == 0) return nullptr;

        uint64_t h = fnv1a_hash(key);
        uint64_t idx = h & (bucket_count() - 1);

        auto* bucket_ptrs = buckets_.get(base);
        if (bucket_ptrs[idx].is_null()) return nullptr;

        auto* bucket = bucket_ptrs[idx].get(base);
        uint32_t bsz = bucket_size(bucket);
        uint32_t bcap = bucket_cap(bucket);
        auto* keys = bucket_keys(bucket);

        for (uint32_t i = 0; i < bsz; ++i) {
            ArenaString* k = keys[i].get(base);
            if (k && *k == key) {
                return &bucket_values(bucket, bcap)[i];
            }
        }
        return nullptr;
    }

    const AnyVal* get_slot(std::string_view key, uint8_t* base) const noexcept {
        return const_cast<ObjectMap*>(this)->get_slot(key, base);
    }

    // --- Iteration ---

    template <typename Fn>
    void for_each(Fn fn, uint8_t* base) const noexcept {
        if (size_ == 0 || bucket_capacity_ == 0) return;

        auto* bucket_ptrs = buckets_.get(base);
        uint64_t count = bucket_count();

        for (uint64_t i = 0; i < count; ++i) {
            if (bucket_ptrs[i].is_null()) continue;

            auto* bucket = bucket_ptrs[i].get(base);
            uint32_t bsz = bucket_size(bucket);
            uint32_t bcap = bucket_cap(bucket);
            auto* keys = bucket_keys(bucket);
            auto* vals = bucket_values(bucket, bcap);

            for (uint32_t j = 0; j < bsz; ++j) {
                ArenaString* k = keys[j].get(base);
                if (k) fn(k, &vals[j]);
            }
        }
    }

    // --- Mutation ---

    [[nodiscard]] logos::expected<void> put(std::string_view key, AnyVal value, Arena& arena) noexcept {
        uint8_t* base = arena.head().data();

        if (bucket_capacity_ == 0) {
            LOGOS_TRY_VOID(init_buckets(arena));
            base = arena.head().data();
        }

        if (size_ * 4 >= bucket_count() * 3) {
            LOGOS_TRY_VOID(rehash(arena, bucket_capacity_ + 1));
            base = arena.head().data();
        }

        uint64_t h = fnv1a_hash(key);
        uint64_t idx = h & (bucket_count() - 1);

        auto* bucket_ptrs = buckets_.get(base);

        if (bucket_ptrs[idx].is_null()) {
            LOGOS_TRY(auto* bucket_new, create_bucket(arena));
            base = arena.head().data(); // re-derive
            bucket_ptrs = buckets_.get(base);
            bucket_ptrs[idx].set(bucket_new, base);
        }

        auto* bucket = bucket_ptrs[idx].get(base);
        uint32_t bsz = bucket_size(bucket);
        uint32_t bcap = bucket_cap(bucket);
        auto* keys = bucket_keys(bucket);
        auto* vals = bucket_values(bucket, bcap);

        for (uint32_t i = 0; i < bsz; ++i) {
            ArenaString* k = keys[i].get(base);
            if (k && *k == key) {
                vals[i] = value;
                return {};
            }
        }

        // Pre-allocate key string, then re-derive everything.
        LOGOS_TRY(auto* arena_key, ArenaString::create(arena, key));
        base = arena.head().data();
        bucket_ptrs = buckets_.get(base);
        bucket = bucket_ptrs[idx].get(base);
        bsz = bucket_size(bucket);
        bcap = bucket_cap(bucket);

        if (bsz >= bcap) {
            LOGOS_TRY(auto* new_bucket, grow_bucket(arena, bucket, bucket_ptrs, idx, base));
            (void)new_bucket;
            base = arena.head().data();
            bucket = buckets_.get(base)[idx].get(base);
            bsz = bucket_size(bucket);
            bcap = bucket_cap(bucket);
        }

        keys = bucket_keys(bucket);
        vals = bucket_values(bucket, bcap);
        keys[bsz].set(arena_key, base);
        vals[bsz] = value;
        set_bucket_size(bucket, bsz + 1);
        ++size_;
        return {};
    }

    bool remove(std::string_view key, Arena& arena) noexcept {
        if (size_ == 0) return false;
        uint8_t* base = arena.head().data();

        uint64_t h = fnv1a_hash(key);
        uint64_t idx = h & (bucket_count() - 1);

        auto* bucket_ptrs = buckets_.get(base);
        if (bucket_ptrs[idx].is_null()) return false;

        auto* bucket = bucket_ptrs[idx].get(base);
        uint32_t bsz = bucket_size(bucket);
        uint32_t bcap = bucket_cap(bucket);
        auto* keys = bucket_keys(bucket);
        auto* vals = bucket_values(bucket, bcap);

        for (uint32_t i = 0; i < bsz; ++i) {
            ArenaString* k = keys[i].get(base);
            if (k && *k == key) {
                if (i + 1 < bsz) {
                    keys[i] = keys[bsz - 1];
                    vals[i] = vals[bsz - 1];
                }
                keys[bsz - 1].clear();
                vals[bsz - 1] = AnyVal{};
                set_bucket_size(bucket, bsz - 1);
                --size_;
                return true;
            }
        }
        return false;
    }

    // --- Factory ---

    [[nodiscard]] static logos::expected<ObjectMap*> create(Arena& arena, uint8_t initial_log2_buckets = 3) noexcept {
        TypeTag tag(type_hash::ObjectMap, TagDescriptor::Map);
        LOGOS_TRY(auto* mem, arena.allocate(sizeof(ObjectMap), alignof(ObjectMap), tag));
        auto* map = new (mem) ObjectMap();
        if (initial_log2_buckets > 0) {
            LOGOS_TRY_VOID(map->init_buckets(arena));
        }
        return map;
    }

private:
    uint64_t size_ = 0;
    uint64_t bucket_capacity_ = 0;
    RelativePtr<RelativePtr<uint8_t>> buckets_;

    static constexpr size_t bucket_header_size = 8;

    static size_t bucket_alloc_size(uint32_t cap) noexcept {
        return bucket_header_size
             + cap * sizeof(RelativePtr<ArenaString>)
             + cap * sizeof(AnyVal);
    }

    static uint32_t bucket_size(const uint8_t* bucket) noexcept {
        uint32_t s; std::memcpy(&s, bucket, 4); return s;
    }
    static uint32_t bucket_cap(const uint8_t* bucket) noexcept {
        uint32_t c; std::memcpy(&c, bucket + 4, 4); return c;
    }
    static void set_bucket_size(uint8_t* bucket, uint32_t s) noexcept {
        std::memcpy(bucket, &s, 4);
    }

    static RelativePtr<ArenaString>* bucket_keys(uint8_t* bucket) noexcept {
        return reinterpret_cast<RelativePtr<ArenaString>*>(bucket + bucket_header_size);
    }
    static const RelativePtr<ArenaString>* bucket_keys(const uint8_t* bucket) noexcept {
        return reinterpret_cast<const RelativePtr<ArenaString>*>(bucket + bucket_header_size);
    }

    static AnyVal* bucket_values(uint8_t* bucket, uint32_t cap) noexcept {
        return reinterpret_cast<AnyVal*>(bucket + bucket_header_size + cap * sizeof(RelativePtr<ArenaString>));
    }
    static const AnyVal* bucket_values(const uint8_t* bucket, uint32_t cap) noexcept {
        return reinterpret_cast<const AnyVal*>(bucket + bucket_header_size + cap * sizeof(RelativePtr<ArenaString>));
    }

    logos::expected<uint8_t*> create_bucket(Arena& arena, uint32_t cap = 4) noexcept {
        size_t alloc = bucket_alloc_size(cap);
        LOGOS_TRY(auto* bucket_void, arena.allocate_raw(alloc, 8));
        auto* bucket = static_cast<uint8_t*>(bucket_void);
        std::memset(bucket, 0, alloc);
        // Set NULL_OFFSET for all RelativePtrs (zero won't work since NULL_OFFSET = ~0).
        auto* keys = bucket_keys(bucket);
        for (uint32_t i = 0; i < cap; ++i) keys[i] = RelativePtr<ArenaString>{};
        uint32_t zero = 0;
        std::memcpy(bucket, &zero, 4);
        std::memcpy(bucket + 4, &cap, 4);
        return bucket;
    }

    logos::expected<uint8_t*> grow_bucket(Arena& arena, const uint8_t* old_bucket,
                                          RelativePtr<uint8_t>* bucket_ptrs, uint64_t idx,
                                          uint8_t* base) noexcept {
        uint32_t old_sz = bucket_size(old_bucket);
        uint32_t old_cap = bucket_cap(old_bucket);
        uint32_t new_cap = old_cap * 2;

        LOGOS_TRY(auto* new_bucket, create_bucket(arena, new_cap));
        base = arena.head().data(); // re-derive

        // Re-derive old_bucket from its offset.
        auto* bptrs = buckets_.get(base);
        old_bucket = bptrs[idx].get(base);

        auto* old_keys = bucket_keys(old_bucket);
        auto* old_vals = bucket_values(old_bucket, old_cap);
        auto* new_keys = bucket_keys(new_bucket);
        auto* new_vals = bucket_values(new_bucket, new_cap);

        // Segment-relative: just copy, no relocation!
        std::memcpy(new_keys, old_keys, old_sz * sizeof(RelativePtr<ArenaString>));
        std::memcpy(new_vals, old_vals, old_sz * sizeof(AnyVal));
        set_bucket_size(new_bucket, old_sz);

        bptrs[idx].set(new_bucket, base);
        return new_bucket;
    }

    logos::expected<void> init_buckets(Arena& arena, uint64_t log2_cap = 3) noexcept {
        uint64_t count = 1ULL << log2_cap;
        size_t alloc = count * sizeof(RelativePtr<uint8_t>);
        LOGOS_TRY(auto* mem_void, arena.allocate_raw(alloc, 8));
        auto* mem = static_cast<RelativePtr<uint8_t>*>(mem_void);
        for (uint64_t i = 0; i < count; ++i) mem[i] = RelativePtr<uint8_t>{};
        uint8_t* base = arena.head().data();
        buckets_.set(mem, base);
        bucket_capacity_ = log2_cap;
        return {};
    }

    logos::expected<void> rehash(Arena& arena, uint64_t new_log2_cap) noexcept {
        uint64_t old_count = bucket_count();
        uint8_t* base = arena.head().data();
        auto* old_bucket_ptrs = buckets_.get(base);

        uint64_t new_count = 1ULL << new_log2_cap;
        size_t alloc = new_count * sizeof(RelativePtr<uint8_t>);
        LOGOS_TRY(auto* new_mem_void, arena.allocate_raw(alloc, 8));
        auto* new_bucket_ptrs = static_cast<RelativePtr<uint8_t>*>(new_mem_void);
        for (uint64_t i = 0; i < new_count; ++i) new_bucket_ptrs[i] = RelativePtr<uint8_t>{};

        base = arena.head().data(); // re-derive
        buckets_.set(new_bucket_ptrs, base);
        bucket_capacity_ = new_log2_cap;
        size_ = 0;

        for (uint64_t i = 0; i < old_count; ++i) {
            if (old_bucket_ptrs[i].is_null()) continue;

            auto* bucket = old_bucket_ptrs[i].get(base);
            uint32_t bsz = bucket_size(bucket);
            uint32_t bcap = bucket_cap(bucket);
            auto* keys = bucket_keys(bucket);
            auto* vals = bucket_values(bucket, bcap);

            for (uint32_t j = 0; j < bsz; ++j) {
                ArenaString* k = keys[j].get(base);
                if (!k) continue;

                uint64_t h = k->hash();
                uint64_t idx = h & (new_count - 1);

                new_bucket_ptrs = buckets_.get(base);
                if (new_bucket_ptrs[idx].is_null()) {
                    LOGOS_TRY(auto* nb_void, create_bucket(arena));
                    auto* nb = nb_void;
                    base = arena.head().data();
                    new_bucket_ptrs = buckets_.get(base);
                    new_bucket_ptrs[idx].set(nb, base);
                }

                auto* nb = new_bucket_ptrs[idx].get(base);
                uint32_t nsz = bucket_size(nb);
                uint32_t ncap = bucket_cap(nb);

                if (nsz >= ncap) {
                    LOGOS_TRY(auto* grown_nb, grow_bucket(arena, nb, new_bucket_ptrs, idx, base));
                    (void)grown_nb;
                    base = arena.head().data();
                    new_bucket_ptrs = buckets_.get(base);
                    nb = new_bucket_ptrs[idx].get(base);
                    nsz = bucket_size(nb);
                    ncap = bucket_cap(nb);
                }

                bucket_keys(nb)[nsz] = keys[j]; // Copy offset directly
                bucket_values(nb, ncap)[nsz] = vals[j]; // Copy AnyVal directly
                set_bucket_size(nb, nsz + 1);
                ++size_;
            }
        }
        return {};
    }
};

// size_(8) + bucket_capacity_(8) + buckets_(4) + padding(4) = 24
static_assert(sizeof(ObjectMap) == 24);

} // namespace logos::hermes
