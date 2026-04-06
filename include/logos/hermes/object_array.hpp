// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <cstring>
#include <new>

#include <logos/hermes/arena.hpp>
#include <logos/hermes/relative_ptr.hpp>
#include <logos/hermes/any_val.hpp>
#include <logos/hermes/type_registry.hpp>
#include <logos/core/expected.hpp>

namespace logos::hermes {

// ObjectArray: dynamic array of heterogeneous objects (AnyVal elements).
// All read/write methods take `uint8_t* base` — the segment base address.
class ObjectArray {
public:
    uint64_t size() const noexcept     { return size_; }
    uint64_t capacity() const noexcept { return capacity_; }
    bool empty() const noexcept        { return size_ == 0; }

    AnyVal get(uint64_t index, uint8_t* base) const noexcept {
        if (index >= size_) return AnyVal{};
        return elements(base)[index];
    }

    AnyVal* slot(uint64_t index, uint8_t* base) noexcept {
        if (index >= size_) return nullptr;
        return &elements(base)[index];
    }

    [[nodiscard]] logos::expected<void> push_back(AnyVal value, Arena& arena) noexcept {
        // Save our offset from the arena base before any allocation.
        // GrowableSingleChunk may realloc its buffer, making `this` stale.
        ptrdiff_t self_off = reinterpret_cast<uint8_t*>(this) - arena.head().data();
        if (size_ >= capacity_) {
            LOGOS_TRY_VOID(grow(arena, capacity_ == 0 ? 4 : capacity_ * 2));
        }
        uint8_t* base = arena.head().data();
        auto* self = reinterpret_cast<ObjectArray*>(base + self_off);
        self->elements(base)[self->size_] = value;
        ++self->size_;
        return {};
    }

    void set(uint64_t index, AnyVal value, uint8_t* base) noexcept {
        if (index < size_) {
            elements(base)[index] = value;
        }
    }

    void pop_back(uint8_t* base) noexcept {
        if (size_ > 0) {
            --size_;
            elements(base)[size_] = AnyVal{};
        }
    }

    [[nodiscard]] static logos::expected<ObjectArray*> create(Arena& arena, uint64_t initial_capacity = 4) noexcept {
        TypeTag tag(type_hash::ObjectArray, TagDescriptor::Array);
        LOGOS_TRY(auto* mem, arena.allocate(sizeof(ObjectArray), alignof(ObjectArray), tag));
        auto* arr = new (mem) ObjectArray();
        // Save offset after allocation so we can recompute if grow() reallocs the buffer.
        ptrdiff_t arr_off = reinterpret_cast<uint8_t*>(arr) - arena.head().data();
        if (initial_capacity > 0) {
            LOGOS_TRY_VOID(arr->grow(arena, initial_capacity));
        }
        return reinterpret_cast<ObjectArray*>(arena.head().data() + arr_off);
    }

private:
    uint64_t size_ = 0;
    uint64_t capacity_ = 0;
    RelativePtr<AnyVal> data_;

    AnyVal* elements(uint8_t* base) const noexcept { return data_.get(base); }

    logos::expected<void> grow(Arena& arena, uint64_t new_cap) noexcept {
        // Save our offset from the arena base BEFORE allocating.
        // GrowableSingleChunk may realloc its buffer during allocate_raw,
        // making `this` a dangling pointer.  Recompute self from new base after.
        uint8_t* base_before = arena.head().data();
        ptrdiff_t self_off = reinterpret_cast<uint8_t*>(this) - base_before;

        LOGOS_TRY(auto* new_mem_void, arena.allocate_raw(new_cap * sizeof(AnyVal), alignof(AnyVal)));
        auto* new_elems = static_cast<AnyVal*>(new_mem_void);
        for (uint64_t i = 0; i < new_cap; ++i) new_elems[i] = AnyVal{};

        uint8_t* base = arena.head().data();
        // Recompute self — for MultiChunk base never moves so self == this;
        // for GrowableSingleChunk after realloc, self points into the new buffer.
        auto* self = reinterpret_cast<ObjectArray*>(base + self_off);

        if (self->size_ > 0 && !self->data_.is_null()) {
            std::memcpy(new_elems, self->elements(base), self->size_ * sizeof(AnyVal));
        }

        self->data_.set(new_elems, base);
        self->capacity_ = new_cap;
        return {};
    }
};

// size_(8) + capacity_(8) + data_(4) + padding(4) = 24
static_assert(sizeof(ObjectArray) == 24);

} // namespace logos::hermes
