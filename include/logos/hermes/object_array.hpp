// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <new>

#include <logos/hermes/arena.hpp>
#include <logos/hermes/relative_ptr.hpp>
#include <logos/hermes/any_val.hpp>
#include <logos/hermes/type_codes.hpp>
#include <logos/core/expected.hpp>

namespace logos::hermes {

// ObjectArray — dynamic array of heterogeneous AnyVal elements. BYTE-IDENTICAL to
// the Logos stdlib HArray<HVal> (stdlib/lang/hermes2/array.logos):
//   { size_/len : u64, capacity_/cap : u64, data_ : self-relative ptr to AnyVal[] }
// 24 bytes. The element buffer holds AT-REST AnyVals (self-relative Ref arm).
//
// Ported from Hermes1's ObjectArray, reseated onto the self-relative RelativePtr +
// 8-byte AnyVal: NO `base` is threaded (self-relative resolves in place), and the
// header NEVER MOVES (MultiChunk arena), so the old offset-recompute dance around
// realloc is gone. Growth copies elements ONE BY ONE so each AnyVal Ref RE-ANCHORS
// to its new slot — a raw memcpy would leave every Ref pointing at the old buffer.
class ObjectArray {
public:
    uint64_t size() const noexcept     { return size_; }
    uint64_t capacity() const noexcept { return capacity_; }
    bool empty() const noexcept        { return size_ == 0; }

    // Returns the element by value; the copy re-anchors its Ref to the result.
    AnyVal get(uint64_t index) const noexcept {
        if (index >= size_) return AnyVal{};
        return elements()[index];
    }
    // CUT-OVER VESTIGIAL (base ignored) — lets logosc base-threading sites compile.
    AnyVal get(uint64_t index, const void*) const noexcept { return get(index); }

    AnyVal* slot(uint64_t index) noexcept {
        return index < size_ ? &elements()[index] : nullptr;
    }

    [[nodiscard]] logos::expected<void> push_back(AnyVal value, Arena& arena) noexcept {
        if (size_ >= capacity_) {
            LOGOS_TRY_VOID(grow(arena, capacity_ == 0 ? 4 : capacity_ * 2));
        }
        elements()[size_] = value;   // assignment lowers (re-anchors) the Ref to the slot
        ++size_;
        return {};
    }

    void set(uint64_t index, AnyVal value) noexcept {
        if (index < size_) elements()[index] = value;
    }

    void pop_back() noexcept {
        if (size_ > 0) { --size_; elements()[size_].set_null(); }
    }

    [[nodiscard]] static logos::expected<ObjectArray*>
    create(Arena& arena, uint64_t initial_capacity = 4) noexcept {
        TypeTag tag(tc::ARRAY);
        LOGOS_TRY(auto* mem, arena.allocate(sizeof(ObjectArray), alignof(ObjectArray), tag));
        auto* arr = new (mem) ObjectArray();
        if (initial_capacity > 0) {
            LOGOS_TRY_VOID(arr->grow(arena, initial_capacity));
        }
        return arr;
    }

private:
    uint64_t            size_ = 0;
    uint64_t            capacity_ = 0;
    RelativePtr<AnyVal> data_;

    AnyVal* elements() const noexcept { return data_.get(); }

    logos::expected<void> grow(Arena& arena, uint64_t new_cap) noexcept {
        // The header never moves (MultiChunk); only `data_` is re-pointed.
        LOGOS_TRY(auto* mem, arena.allocate_raw(new_cap * sizeof(AnyVal), alignof(AnyVal)));
        auto* new_elems = static_cast<AnyVal*>(mem);
        for (uint64_t i = 0; i < new_cap; ++i) new (&new_elems[i]) AnyVal();
        // Re-anchoring copy (NOT memcpy): each Ref re-lowers to its new slot.
        if (size_ > 0 && data_.is_not_null()) {
            AnyVal* old = elements();
            for (uint64_t i = 0; i < size_; ++i) new_elems[i] = old[i];
        }
        data_ = new_elems;           // RelativePtr::operator=(T*) lowers self-relative
        capacity_ = new_cap;
        return {};
    }
};

// size_(8) + capacity_(8) + data_(8, self-relative i64) = 24 (matches HArray<HVal>)
static_assert(sizeof(ObjectArray) == 24);

} // namespace logos::hermes
