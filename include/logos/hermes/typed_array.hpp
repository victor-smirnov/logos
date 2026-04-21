// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>

#include <logos/hermes/arena.hpp>
#include <logos/hermes/relative_ptr.hpp>
#include <logos/hermes/type_registry.hpp>
#include <logos/core/expected.hpp>

namespace logos::hermes {

// TypedArray<T>: dense array of trivially-copyable primitive T, byte-compatible
// with stdlib/hermes/array.logos Array<T>.
//
// Header layout (24 bytes, same as ObjectArray):
//   uint64_t size_
//   uint64_t capacity_
//   RelativePtr<T> data_   (u32) + 4 bytes trailing padding
//
// Logos maps concrete specialisations to type_code:
//   Array<i32>   → 104
//   Array<u64>   → 108
//   ... (extend type_code_for as more specialisations are needed)
//
// All read/write methods take `uint8_t* base` — the segment base address.
template <typename T>
class TypedArray {
    static_assert(std::is_trivially_copyable_v<T>,
                  "TypedArray<T> requires trivially-copyable T");

public:
    uint64_t size()     const noexcept { return size_; }
    uint64_t capacity() const noexcept { return capacity_; }
    bool     empty()    const noexcept { return size_ == 0; }

    T get(uint64_t index, uint8_t* base) const noexcept {
        return elements(base)[index];
    }

    T* slot(uint64_t index, uint8_t* base) noexcept {
        if (index >= size_) return nullptr;
        return &elements(base)[index];
    }

    [[nodiscard]] logos::expected<void> push_back(T value, Arena& arena) noexcept {
        ptrdiff_t self_off = reinterpret_cast<uint8_t*>(this) - arena.head().data();
        if (size_ >= capacity_) {
            LOGOS_TRY_VOID(grow(arena, capacity_ == 0 ? 4 : capacity_ * 2));
        }
        uint8_t* base = arena.head().data();
        auto* self = reinterpret_cast<TypedArray*>(base + self_off);
        self->elements(base)[self->size_] = value;
        ++self->size_;
        return {};
    }

    void set(uint64_t index, T value, uint8_t* base) noexcept {
        if (index < size_) elements(base)[index] = value;
    }

    [[nodiscard]] static logos::expected<TypedArray*> create(
            Arena& arena, uint64_t initial_capacity = 4) noexcept {
        TypeTag tag(type_code_for(), TagDescriptor::Array);
        LOGOS_TRY(auto* mem, arena.allocate(sizeof(TypedArray), alignof(TypedArray), tag));
        auto* arr = new (mem) TypedArray();
        ptrdiff_t arr_off = reinterpret_cast<uint8_t*>(arr) - arena.head().data();
        if (initial_capacity > 0) {
            LOGOS_TRY_VOID(arr->grow(arena, initial_capacity));
        }
        return reinterpret_cast<TypedArray*>(arena.head().data() + arr_off);
    }

private:
    uint64_t size_     = 0;
    uint64_t capacity_ = 0;
    RelativePtr<T> data_;

    T* elements(uint8_t* base) const noexcept { return data_.get(base); }

    // Concrete Logos type_code binding per T (Array<T> wire-format code).
    static constexpr uint64_t type_code_for() noexcept {
        if constexpr (std::is_same_v<T, int32_t>)  return 104; // Array<i32>
        if constexpr (std::is_same_v<T, uint64_t>) return 108; // Array<u64>
        return 0; // unregistered; caller must supply extrinsic type_code.
    }

    logos::expected<void> grow(Arena& arena, uint64_t new_cap) noexcept {
        uint8_t* base_before = arena.head().data();
        ptrdiff_t self_off = reinterpret_cast<uint8_t*>(this) - base_before;

        LOGOS_TRY(auto* new_mem_void, arena.allocate_raw(new_cap * sizeof(T), alignof(T)));
        auto* new_elems = static_cast<T*>(new_mem_void);
        std::memset(new_elems, 0, new_cap * sizeof(T));

        uint8_t* base = arena.head().data();
        auto* self = reinterpret_cast<TypedArray*>(base + self_off);
        if (self->size_ > 0 && !self->data_.is_null()) {
            std::memcpy(new_elems, self->elements(base), self->size_ * sizeof(T));
        }
        self->data_.set(new_elems, base);
        self->capacity_ = new_cap;
        return {};
    }
};

static_assert(sizeof(TypedArray<int32_t>) == 24);
static_assert(sizeof(TypedArray<uint64_t>) == 24);

using ArrayI32 = TypedArray<int32_t>;
using ArrayU64 = TypedArray<uint64_t>;

} // namespace logos::hermes
