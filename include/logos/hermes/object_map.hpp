// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>

#include <logos/hermes/arena.hpp>
#include <logos/hermes/relative_ptr.hpp>
#include <logos/hermes/tagged_ptr.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/fnv_hash.hpp>
#include <logos/hermes/type_registry.hpp>

namespace logos::hermes {

// ObjectMap: string-keyed hash map with TaggedPtr values.
//
// Arena layout (24 bytes + tag):
//   [TypeTag: 2 bytes before]
//   size_:             uint64_t    (total entry count)
//   bucket_capacity_:  uint64_t    (log2 of bucket array length)
//   buckets_:          RelativePtr<RelativePtr<Bucket>[]>
//
// Each bucket is an untagged arena object with SoA layout:
//   [size: uint32_t] [capacity: uint32_t]
//   [keys: RelativePtr<ArenaString>[capacity]]
//   [values: TaggedPtr[capacity]]
class ObjectMap {
public:
    uint64_t size() const { return size_; }
    bool empty() const    { return size_ == 0; }

    uint64_t bucket_count() const { return 1ULL << bucket_capacity_; }

    // --- Lookup ---

    // Find value by string key. Returns null TaggedPtr if absent.
    TaggedPtr get(std::string_view key) const {
        if (size_ == 0) return TaggedPtr{};

        uint64_t h = fnv1a_hash(key);
        uint64_t idx = h & (bucket_count() - 1);

        auto* bucket_ptrs = buckets_.get();
        if (bucket_ptrs[idx].is_null()) return TaggedPtr{};

        auto* bucket = bucket_entry(bucket_ptrs, idx);
        uint32_t bsz = bucket_size(bucket);
        auto* keys = bucket_keys(bucket);

        for (uint32_t i = 0; i < bsz; ++i) {
            ArenaString* k = keys[i].get();
            if (k && *k == key) {
                return bucket_values(bucket, bucket_cap(bucket))[i];
            }
        }
        return TaggedPtr{};
    }

    bool has(std::string_view key) const {
        return get_slot(key) != nullptr;
    }

    // Get a mutable pointer to the value slot for a key.
    // Returns nullptr if the key is absent. Use this instead of get()
    // when the value might be in pointer mode.
    TaggedPtr* get_slot(std::string_view key) {
        if (size_ == 0) return nullptr;

        uint64_t h = fnv1a_hash(key);
        uint64_t idx = h & (bucket_count() - 1);

        auto* bucket_ptrs = buckets_.get();
        if (bucket_ptrs[idx].is_null()) return nullptr;

        auto* bucket = bucket_entry(bucket_ptrs, idx);
        uint32_t bsz = bucket_size(bucket);
        uint32_t bcap = bucket_cap(bucket);
        auto* keys = bucket_keys(bucket);

        for (uint32_t i = 0; i < bsz; ++i) {
            ArenaString* k = keys[i].get();
            if (k && *k == key) {
                return &bucket_values(bucket, bcap)[i];
            }
        }
        return nullptr;
    }

    const TaggedPtr* get_slot(std::string_view key) const {
        return const_cast<ObjectMap*>(this)->get_slot(key);
    }

    // --- Iteration ---

    // Call fn(ArenaString* key, TaggedPtr* value_slot) for each entry.
    template <typename Fn>
    void for_each(Fn fn) const {
        if (size_ == 0 || bucket_capacity_ == 0) return;

        auto* bucket_ptrs = buckets_.get();
        uint64_t count = bucket_count();

        for (uint64_t i = 0; i < count; ++i) {
            if (bucket_ptrs[i].is_null()) continue;

            auto* bucket = bucket_ptrs[i].get();
            uint32_t bsz = bucket_size(bucket);
            uint32_t bcap = bucket_cap(bucket);
            auto* keys = bucket_keys(bucket);
            auto* vals = bucket_values(bucket, bcap);

            for (uint32_t j = 0; j < bsz; ++j) {
                ArenaString* k = keys[j].get();
                if (k) fn(k, &vals[j]);
            }
        }
    }

    // --- Mutation ---

    // Insert or update a key-value pair. Key is allocated as ArenaString if new.
    void put(std::string_view key, TaggedPtr value, Arena& arena) {
        if (bucket_capacity_ == 0) {
            init_buckets(arena, 3); // Start with 8 buckets.
        }

        // Check load factor and rehash if needed (> 75% full).
        if (size_ * 4 >= bucket_count() * 3) {
            rehash(arena, bucket_capacity_ + 1);
        }

        uint64_t h = fnv1a_hash(key);
        uint64_t idx = h & (bucket_count() - 1);

        auto* bucket_ptrs = buckets_.get();

        if (bucket_ptrs[idx].is_null()) {
            auto* bucket = create_bucket(arena, 4);
            bucket_ptrs[idx].set(bucket);
        }

        auto* bucket = bucket_entry(bucket_ptrs, idx);
        uint32_t bsz = bucket_size(bucket);
        uint32_t bcap = bucket_cap(bucket);
        auto* keys = bucket_keys(bucket);
        auto* vals = bucket_values(bucket, bcap);

        // Check for existing key.
        for (uint32_t i = 0; i < bsz; ++i) {
            ArenaString* k = keys[i].get();
            if (k && *k == key) {
                vals[i] = value;
                return;
            }
        }

        // Pre-allocate the key string BEFORE any bucket operations,
        // since arena allocation can invalidate pointers in GrowableSingleChunk mode.
        ArenaString* arena_key = ArenaString::create(arena, key);

        // Re-fetch bucket pointers (may have been invalidated by string allocation).
        bucket_ptrs = buckets_.get();
        bucket = bucket_entry(bucket_ptrs, idx);
        bsz = bucket_size(bucket);
        bcap = bucket_cap(bucket);

        // Grow bucket if needed.
        if (bsz >= bcap) {
            bucket = grow_bucket(arena, bucket, bucket_ptrs, idx);
            bsz = bucket_size(bucket);
            bcap = bucket_cap(bucket);
        }

        keys = bucket_keys(bucket);
        vals = bucket_values(bucket, bcap);
        keys[bsz].set(arena_key);
        vals[bsz] = value;
        set_bucket_size(bucket, bsz + 1);
        ++size_;
    }

    // Remove a key. Returns true if it was present.
    bool remove(std::string_view key, Arena& /*arena*/) {
        if (size_ == 0) return false;

        uint64_t h = fnv1a_hash(key);
        uint64_t idx = h & (bucket_count() - 1);

        auto* bucket_ptrs = buckets_.get();
        if (bucket_ptrs[idx].is_null()) return false;

        auto* bucket = bucket_entry(bucket_ptrs, idx);
        uint32_t bsz = bucket_size(bucket);
        uint32_t bcap = bucket_cap(bucket);
        auto* keys = bucket_keys(bucket);
        auto* vals = bucket_values(bucket, bcap);

        for (uint32_t i = 0; i < bsz; ++i) {
            ArenaString* k = keys[i].get();
            if (k && *k == key) {
                // Swap with last element.
                if (i + 1 < bsz) {
                    keys[i] = keys[bsz - 1];
                    vals[i] = vals[bsz - 1];
                }
                keys[bsz - 1].clear();
                vals[bsz - 1] = TaggedPtr{};
                set_bucket_size(bucket, bsz - 1);
                --size_;
                return true;
            }
        }
        return false;
    }

    // --- Factory ---

    static ObjectMap* create(Arena& arena, uint8_t initial_log2_buckets = 3) {
        TypeTag tag(type_hash::ObjectMap, TagDescriptor::Map);
        void* mem = arena.allocate(sizeof(ObjectMap), alignof(ObjectMap), tag);
        auto* map = new (mem) ObjectMap();

        if (initial_log2_buckets > 0) {
            map->init_buckets(arena, initial_log2_buckets);
        }
        return map;
    }

private:
    uint64_t size_ = 0;
    uint64_t bucket_capacity_ = 0;  // log2 of bucket array length
    RelativePtr<RelativePtr<uint8_t>> buckets_;  // array of bucket pointers

    // --- Bucket layout helpers ---
    // Bucket is untagged: [size:u32][cap:u32][keys:RelativePtr<ArenaString>*cap][values:TaggedPtr*cap]

    static constexpr size_t bucket_header_size = 8; // size(4) + capacity(4)

    static size_t bucket_alloc_size(uint32_t cap) {
        return bucket_header_size
             + cap * sizeof(RelativePtr<ArenaString>)
             + cap * sizeof(TaggedPtr);
    }

    static uint32_t bucket_size(const uint8_t* bucket) {
        uint32_t s;
        std::memcpy(&s, bucket, 4);
        return s;
    }

    static uint32_t bucket_cap(const uint8_t* bucket) {
        uint32_t c;
        std::memcpy(&c, bucket + 4, 4);
        return c;
    }

    static void set_bucket_size(uint8_t* bucket, uint32_t s) {
        std::memcpy(bucket, &s, 4);
    }

    static RelativePtr<ArenaString>* bucket_keys(uint8_t* bucket) {
        return reinterpret_cast<RelativePtr<ArenaString>*>(bucket + bucket_header_size);
    }

    static const RelativePtr<ArenaString>* bucket_keys(const uint8_t* bucket) {
        return reinterpret_cast<const RelativePtr<ArenaString>*>(bucket + bucket_header_size);
    }

    static TaggedPtr* bucket_values(uint8_t* bucket, uint32_t cap) {
        size_t offset = bucket_header_size + cap * sizeof(RelativePtr<ArenaString>);
        return reinterpret_cast<TaggedPtr*>(bucket + offset);
    }

    static const TaggedPtr* bucket_values(const uint8_t* bucket, uint32_t cap) {
        size_t offset = bucket_header_size + cap * sizeof(RelativePtr<ArenaString>);
        return reinterpret_cast<const TaggedPtr*>(bucket + offset);
    }

    uint8_t* bucket_entry(RelativePtr<uint8_t>* bucket_ptrs, uint64_t idx) const {
        return bucket_ptrs[idx].get();
    }

    const uint8_t* bucket_entry(const RelativePtr<uint8_t>* bucket_ptrs, uint64_t idx) const {
        return bucket_ptrs[idx].get();
    }

    uint8_t* create_bucket(Arena& arena, uint32_t cap) {
        size_t alloc = bucket_alloc_size(cap);
        auto* bucket = static_cast<uint8_t*>(arena.allocate_raw(alloc, 8));
        // Zero-init the raw bytes (all RelativePtrs and TaggedPtrs are null when zero).
        std::memset(bucket, 0, alloc);
        uint32_t zero = 0;
        std::memcpy(bucket, &zero, 4);     // size = 0
        std::memcpy(bucket + 4, &cap, 4);  // capacity = cap
        return bucket;
    }

    uint8_t* grow_bucket(Arena& arena, const uint8_t* old_bucket,
                         RelativePtr<uint8_t>* bucket_ptrs, uint64_t idx) {
        uint32_t old_sz = bucket_size(old_bucket);
        uint32_t old_cap = bucket_cap(old_bucket);
        uint32_t new_cap = old_cap * 2;

        auto* new_bucket = create_bucket(arena, new_cap);

        // Copy entries.
        auto* old_keys = bucket_keys(old_bucket);
        auto* old_vals = bucket_values(old_bucket, old_cap);
        auto* new_keys = bucket_keys(new_bucket);
        auto* new_vals = bucket_values(new_bucket, new_cap);

        for (uint32_t i = 0; i < old_sz; ++i) {
            new_keys[i].set(old_keys[i].get());
            new_vals[i] = old_vals[i];
            new_vals[i].relocate_from(&old_vals[i]);
        }
        set_bucket_size(new_bucket, old_sz);

        bucket_ptrs[idx].set(new_bucket);
        return new_bucket;
    }

    void init_buckets(Arena& arena, uint64_t log2_cap) {
        uint64_t count = 1ULL << log2_cap;
        size_t alloc = count * sizeof(RelativePtr<uint8_t>);
        auto* mem = static_cast<RelativePtr<uint8_t>*>(arena.allocate_raw(alloc, 8));
        for (uint64_t i = 0; i < count; ++i) mem[i] = RelativePtr<uint8_t>{};
        buckets_.set(mem);
        bucket_capacity_ = log2_cap;
    }

    void rehash(Arena& arena, uint64_t new_log2_cap) {
        uint64_t old_count = bucket_count();
        auto* old_bucket_ptrs = buckets_.get();

        // Allocate new bucket array.
        uint64_t new_count = 1ULL << new_log2_cap;
        size_t alloc = new_count * sizeof(RelativePtr<uint8_t>);
        auto* new_bucket_ptrs = static_cast<RelativePtr<uint8_t>*>(arena.allocate_raw(alloc, 8));
        for (uint64_t i = 0; i < new_count; ++i) new_bucket_ptrs[i] = RelativePtr<uint8_t>{};

        buckets_.set(new_bucket_ptrs);
        bucket_capacity_ = new_log2_cap;
        size_ = 0;

        // Re-insert all entries.
        for (uint64_t i = 0; i < old_count; ++i) {
            if (old_bucket_ptrs[i].is_null()) continue;

            auto* bucket = old_bucket_ptrs[i].get();
            uint32_t bsz = bucket_size(bucket);
            uint32_t bcap = bucket_cap(bucket);
            auto* keys = bucket_keys(bucket);
            auto* vals = bucket_values(bucket, bcap);

            for (uint32_t j = 0; j < bsz; ++j) {
                ArenaString* k = keys[j].get();
                if (!k) continue;

                // Direct insert (bypass key allocation — reuse existing ArenaString).
                uint64_t h = k->hash();
                uint64_t idx = h & (new_count - 1);

                if (new_bucket_ptrs[idx].is_null()) {
                    auto* nb = create_bucket(arena, 4);
                    new_bucket_ptrs[idx].set(nb);
                }

                auto* nb = new_bucket_ptrs[idx].get();
                uint32_t nsz = bucket_size(nb);
                uint32_t ncap = bucket_cap(nb);

                if (nsz >= ncap) {
                    nb = grow_bucket(arena, nb, new_bucket_ptrs, idx);
                    nsz = bucket_size(nb);
                    ncap = bucket_cap(nb);
                }

                bucket_keys(nb)[nsz].set(k);
                auto* dest_val = &bucket_values(nb, ncap)[nsz];
                *dest_val = vals[j];
                dest_val->relocate_from(&vals[j]);
                set_bucket_size(nb, nsz + 1);
                ++size_;
            }
        }
    }
};

static_assert(sizeof(ObjectMap) == 24);
static_assert(alignof(ObjectMap) == 8);

} // namespace logos::hermes
