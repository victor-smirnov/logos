#pragma once

#include <cstdint>
#include <bit>

#include <logos/hermes/arena.hpp>
#include <logos/hermes/relative_ptr.hpp>
#include <logos/hermes/tagged_ptr.hpp>
#include <logos/hermes/type_registry.hpp>

namespace logos::hermes {

// TinyObjectMap: bitmap-indexed sparse map with uint8_t keys (0..51)
// and TaggedPtr values. O(1) lookup via popcount.
//
// Arena layout (16 bytes + tag):
//   [TypeTag: 2 bytes before]
//   header_: uint64_t          (bitmap + capacity + size packed into 64 bits)
//   data_:   RelativePtr<TaggedPtr[]>  (pointer to value array)
//
// Header bit layout:
//   bits [51:0]  = key bitmap (bit K set → key K present)
//   bits [57:52] = capacity   (allocated slots in value array, max 52)
//   bits [63:58] = size       (number of entries currently stored, max 52)
class TinyObjectMap {
public:
    static constexpr uint8_t  MAX_KEYS     = 52;
    static constexpr uint64_t BITMAP_MASK  = (1ULL << 52) - 1;

    // --- Read access ---

    uint8_t size() const     { return static_cast<uint8_t>((header_ >> 58) & 0x3F); }
    uint8_t capacity() const { return static_cast<uint8_t>((header_ >> 52) & 0x3F); }
    uint64_t bitmap() const  { return header_ & BITMAP_MASK; }

    bool has_key(uint8_t key) const {
        if (key >= MAX_KEYS) return false;
        return (header_ & (1ULL << key)) != 0;
    }

    // Get value for key. Returns null TaggedPtr if key is absent.
    // WARNING: returns by value — for pointer-mode entries, use slot() instead.
    TaggedPtr get(uint8_t key) const {
        if (!has_key(key)) return TaggedPtr{};
        return values()[index_of(key)];
    }

    // Get a mutable pointer to the value slot for a key.
    // Returns nullptr if the key is absent. Use for pointer-mode entries.
    TaggedPtr* slot(uint8_t key) {
        if (!has_key(key)) return nullptr;
        return &values()[index_of(key)];
    }

    const TaggedPtr* slot(uint8_t key) const {
        if (!has_key(key)) return nullptr;
        return &values()[index_of(key)];
    }

    // --- Mutation (requires arena for growth) ---

    // Put a value at key. Grows the value array if needed.
    void put(uint8_t key, TaggedPtr value, Arena& arena) {
        if (key >= MAX_KEYS) return;

        if (has_key(key)) {
            // Update existing entry.
            values()[index_of(key)] = value;
            return;
        }

        // Insert new entry.
        uint8_t sz = size();
        uint8_t cap = capacity();

        if (sz >= cap) {
            grow(arena, cap == 0 ? 4 : cap * 2);
            cap = capacity();
        }

        // Find insertion position (maintain sorted order by popcount position).
        uint8_t pos = index_of(key);

        TaggedPtr* vals = values();
        // Shift elements right to make room.
        for (uint8_t i = sz; i > pos; --i) {
            vals[i] = vals[i - 1];
        }
        vals[pos] = value;

        // Update header: set bit, increment size.
        header_ = (header_ & BITMAP_MASK) | (1ULL << key);         // set key bit
        header_ |= (static_cast<uint64_t>(cap) << 52);              // preserve capacity
        header_ |= (static_cast<uint64_t>(sz + 1) << 58);           // increment size
    }

    // Remove a key. Returns true if the key was present.
    bool remove(uint8_t key) {
        if (!has_key(key)) return false;

        uint8_t pos = index_of(key);
        uint8_t sz = size();
        uint8_t cap = capacity();

        TaggedPtr* vals = values();
        // Shift elements left.
        for (uint8_t i = pos; i + 1 < sz; ++i) {
            vals[i] = vals[i + 1];
        }
        vals[sz - 1] = TaggedPtr{};

        // Update header: clear bit, decrement size.
        uint64_t bm = bitmap() & ~(1ULL << key);
        header_ = bm
                | (static_cast<uint64_t>(cap) << 52)
                | (static_cast<uint64_t>(sz - 1) << 58);

        return true;
    }

    // --- Factory ---

    // Allocate a new TinyObjectMap in the arena with given initial capacity.
    static TinyObjectMap* create(Arena& arena, uint8_t initial_capacity = 4) {
        TypeTag tag(type_hash::Hermes, TagDescriptor::Map);  // type_hash=98, descriptor=2
        void* mem = arena.allocate(sizeof(TinyObjectMap), alignof(TinyObjectMap), tag);
        auto* map = new (mem) TinyObjectMap();

        if (initial_capacity > 0) {
            map->grow(arena, initial_capacity);
        }
        return map;
    }

private:
    uint64_t header_ = 0;
    RelativePtr<TaggedPtr> data_;

    // Compute the position of key in the dense value array.
    // = number of set bits below this key's bit.
    uint8_t index_of(uint8_t key) const {
        uint64_t mask_below = (1ULL << key) - 1;
        return static_cast<uint8_t>(std::popcount(bitmap() & mask_below));
    }

    TaggedPtr* values() const { return data_.get(); }

    void grow(Arena& arena, uint8_t new_cap) {
        if (new_cap > MAX_KEYS) new_cap = MAX_KEYS;

        // Allocate new value array (untagged).
        void* new_mem = arena.allocate_raw(new_cap * sizeof(TaggedPtr), alignof(TaggedPtr));
        auto* new_vals = static_cast<TaggedPtr*>(new_mem);
        for (uint8_t i = 0; i < new_cap; ++i) new_vals[i] = TaggedPtr{};

        // Copy existing values and relocate pointer-mode entries.
        uint8_t sz = size();
        if (sz > 0 && !data_.is_null()) {
            TaggedPtr* old_vals = values();
            for (uint8_t i = 0; i < sz; ++i) {
                new_vals[i] = old_vals[i];
                new_vals[i].relocate_from(&old_vals[i]);
            }
        }

        // Update data pointer and capacity.
        data_.set(new_vals);
        uint64_t bm = bitmap();
        header_ = bm
                | (static_cast<uint64_t>(new_cap) << 52)
                | (static_cast<uint64_t>(sz) << 58);
    }
};

static_assert(sizeof(TinyObjectMap) == 16);
static_assert(alignof(TinyObjectMap) == 8);

} // namespace logos::hermes
