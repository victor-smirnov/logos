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

namespace logos::hermes {

// ObjectArray: dynamic array of heterogeneous objects (AnyVal elements).
// All read/write methods take `uint8_t* base` — the segment base address.
class ObjectArray {
public:
    uint64_t size() const     { return size_; }
    uint64_t capacity() const { return capacity_; }
    bool empty() const        { return size_ == 0; }

    AnyVal get(uint64_t index, uint8_t* base) const {
        if (index >= size_) return AnyVal{};
        return elements(base)[index];
    }

    AnyVal* slot(uint64_t index, uint8_t* base) {
        if (index >= size_) return nullptr;
        return &elements(base)[index];
    }

    void push_back(AnyVal value, Arena& arena) {
        if (size_ >= capacity_) {
            grow(arena, capacity_ == 0 ? 4 : capacity_ * 2);
        }
        elements(arena.head().data())[size_] = value;
        ++size_;
    }

    void set(uint64_t index, AnyVal value, uint8_t* base) {
        if (index < size_) {
            elements(base)[index] = value;
        }
    }

    void pop_back(uint8_t* base) {
        if (size_ > 0) {
            --size_;
            elements(base)[size_] = AnyVal{};
        }
    }

    static ObjectArray* create(Arena& arena, uint64_t initial_capacity = 4) {
        TypeTag tag(type_hash::ObjectArray, TagDescriptor::Array);
        void* mem = arena.allocate(sizeof(ObjectArray), alignof(ObjectArray), tag);
        auto* arr = new (mem) ObjectArray();

        if (initial_capacity > 0) {
            arr->grow(arena, initial_capacity);
        }
        return arr;
    }

private:
    uint64_t size_ = 0;
    uint64_t capacity_ = 0;
    RelativePtr<AnyVal> data_;

    AnyVal* elements(uint8_t* base) const { return data_.get(base); }

    void grow(Arena& arena, uint64_t new_cap) {
        void* new_mem = arena.allocate_raw(new_cap * sizeof(AnyVal), alignof(AnyVal));
        auto* new_elems = static_cast<AnyVal*>(new_mem);
        for (uint64_t i = 0; i < new_cap; ++i) new_elems[i] = AnyVal{};

        uint8_t* base = arena.head().data();
        // Segment-relative: plain copy, no relocation.
        if (size_ > 0 && !data_.is_null()) {
            std::memcpy(new_elems, elements(base), size_ * sizeof(AnyVal));
        }

        data_.set(new_elems, base);
        capacity_ = new_cap;
    }
};

// size_(8) + capacity_(8) + data_(4) + padding(4) = 24
static_assert(sizeof(ObjectArray) == 24);

} // namespace logos::hermes
