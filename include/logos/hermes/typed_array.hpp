// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>

#include <logos/hermes/arena.hpp>
#include <logos/hermes/relative_ptr.hpp>
#include <logos/hermes/type_codes.hpp>
#include <logos/core/expected.hpp>

namespace logos::hermes {

// TypedArray<T> — dense packed array of a trivially-copyable primitive T.
// BYTE-IDENTICAL to the Logos stdlib HArray<T> (stdlib/lang/hermes2/array.logos):
//   { size_/len : u64, capacity_/cap : u64, data_ : self-relative ptr to T[] }  (24B)
//
// Unlike ObjectArray, the elements are PLAIN values (position-independent), so
// growth is a straight memcpy — no per-element re-anchor (there are no self-
// relative Refs in the buffer). Ported from Hermes1's TypedArray onto the self-
// relative RelativePtr: no `base` threaded, header never moves (MultiChunk).
template <typename T>
class TypedArray {
    static_assert(std::is_trivially_copyable_v<T>,
                  "TypedArray<T> requires trivially-copyable T");

public:
    uint64_t size()     const noexcept { return size_; }
    uint64_t capacity() const noexcept { return capacity_; }
    bool     empty()    const noexcept { return size_ == 0; }

    T  get(uint64_t index) const noexcept { return elements()[index]; }
    T* slot(uint64_t index) noexcept { return index < size_ ? &elements()[index] : nullptr; }

    [[nodiscard]] logos::expected<void> push_back(T value, Arena& arena) noexcept {
        if (size_ >= capacity_) {
            LOGOS_TRY_VOID(grow(arena, capacity_ == 0 ? 4 : capacity_ * 2));
        }
        elements()[size_] = value;
        ++size_;
        return {};
    }

    void set(uint64_t index, T value) noexcept {
        if (index < size_) elements()[index] = value;
    }

    // Concrete Hermes2 wire code per T (ArrayU8..ArrayF64 = 2101..2110).
    static constexpr uint64_t type_code_for() noexcept {
        if constexpr (std::is_same_v<T, uint8_t>)  return tc::ARRAY_U8;
        if constexpr (std::is_same_v<T, uint16_t>) return tc::ARRAY_U16;
        if constexpr (std::is_same_v<T, uint32_t>) return tc::ARRAY_U32;
        if constexpr (std::is_same_v<T, uint64_t>) return tc::ARRAY_U64;
        if constexpr (std::is_same_v<T, int8_t>)   return tc::ARRAY_I8;
        if constexpr (std::is_same_v<T, int16_t>)  return tc::ARRAY_I16;
        if constexpr (std::is_same_v<T, int32_t>)  return tc::ARRAY_I32;
        if constexpr (std::is_same_v<T, int64_t>)  return tc::ARRAY_I64;
        if constexpr (std::is_same_v<T, float>)    return tc::ARRAY_F32;
        if constexpr (std::is_same_v<T, double>)   return tc::ARRAY_F64;
        return 0; // unregistered; caller supplies extrinsic code
    }

    [[nodiscard]] static logos::expected<TypedArray*>
    create(Arena& arena, uint64_t initial_capacity = 4) noexcept {
        TypeTag tag(type_code_for());
        LOGOS_TRY(auto* mem, arena.allocate(sizeof(TypedArray), alignof(TypedArray), tag));
        auto* arr = new (mem) TypedArray();
        if (initial_capacity > 0) {
            LOGOS_TRY_VOID(arr->grow(arena, initial_capacity));
        }
        return arr;
    }

private:
    uint64_t       size_     = 0;
    uint64_t       capacity_ = 0;
    RelativePtr<T> data_;

    T* elements() const noexcept { return data_.get(); }

    logos::expected<void> grow(Arena& arena, uint64_t new_cap) noexcept {
        LOGOS_TRY(auto* mem, arena.allocate_raw(new_cap * sizeof(T), alignof(T)));
        auto* new_elems = static_cast<T*>(mem);
        std::memset(new_elems, 0, new_cap * sizeof(T));
        // Plain T elements are position-independent → straight memcpy is correct.
        if (size_ > 0 && data_.is_not_null())
            std::memcpy(new_elems, elements(), size_ * sizeof(T));
        data_ = new_elems;
        capacity_ = new_cap;
        return {};
    }
};

// size_(8) + capacity_(8) + data_(8, self-relative i64) = 24 (matches HArray<T>)
static_assert(sizeof(TypedArray<int8_t>)  == 24);
static_assert(sizeof(TypedArray<double>)  == 24);

using ArrayI8  = TypedArray<int8_t>;
using ArrayU8  = TypedArray<uint8_t>;
using ArrayI16 = TypedArray<int16_t>;
using ArrayU16 = TypedArray<uint16_t>;
using ArrayI32 = TypedArray<int32_t>;
using ArrayU32 = TypedArray<uint32_t>;
using ArrayI64 = TypedArray<int64_t>;
using ArrayU64 = TypedArray<uint64_t>;
using ArrayF32 = TypedArray<float>;
using ArrayF64 = TypedArray<double>;

} // namespace logos::hermes
