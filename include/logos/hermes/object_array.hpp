// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>

#include <logos/hermes/arena.hpp>
#include <logos/hermes/relative_ptr.hpp>
#include <logos/hermes/tagged_ptr.hpp>
#include <logos/hermes/type_registry.hpp>

namespace logos::hermes {

// ObjectArray: dynamic array of heterogeneous objects (TaggedPtr elements).
//
// Uses the "vector" layout: separate data buffer, growable.
//
// Arena layout (24 bytes + tag):
//   [TypeTag: 2 bytes before]
//   size_:     uint64_t                    (current element count)
//   capacity_: uint64_t                    (allocated slot count)
//   data_:     RelativePtr<TaggedPtr[]>    (pointer to element array)
class ObjectArray {
public:
    uint64_t size() const     { return size_; }
    uint64_t capacity() const { return capacity_; }
    bool empty() const        { return size_ == 0; }

    // --- Element access ---

    TaggedPtr get(uint64_t index) const {
        if (index >= size_) return TaggedPtr{};
        return elements()[index];
    }

    TaggedPtr operator[](uint64_t index) const { return get(index); }

    // --- Mutation ---

    // Append an element. Grows if needed.
    void push_back(TaggedPtr value, Arena& arena) {
        if (size_ >= capacity_) {
            grow(arena, capacity_ == 0 ? 4 : capacity_ * 2);
        }
        elements()[size_] = value;
        ++size_;
    }

    // Set element at index (must be < size).
    void set(uint64_t index, TaggedPtr value) {
        if (index < size_) {
            elements()[index] = value;
        }
    }

    // Get a mutable pointer to the slot at index (for in-place set_pointer).
    // Returns nullptr if index >= size.
    TaggedPtr* slot(uint64_t index) {
        if (index >= size_) return nullptr;
        return &elements()[index];
    }

    // Remove last element.
    void pop_back() {
        if (size_ > 0) {
            --size_;
            elements()[size_] = TaggedPtr{};
        }
    }

    // --- Factory ---

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
    RelativePtr<TaggedPtr> data_;

    TaggedPtr* elements() const { return data_.get(); }

    void grow(Arena& arena, uint64_t new_cap) {
        void* new_mem = arena.allocate_raw(new_cap * sizeof(TaggedPtr), alignof(TaggedPtr));
        auto* new_elems = static_cast<TaggedPtr*>(new_mem);
        for (uint64_t i = 0; i < new_cap; ++i) new_elems[i] = TaggedPtr{};

        if (size_ > 0 && !data_.is_null()) {
            TaggedPtr* old_elems = elements();
            for (uint64_t i = 0; i < size_; ++i) {
                new_elems[i] = old_elems[i];
                new_elems[i].relocate_from(&old_elems[i]);
            }
        }

        data_.set(new_elems);
        capacity_ = new_cap;
    }
};

static_assert(sizeof(ObjectArray) == 24);
static_assert(alignof(ObjectArray) == 8);

} // namespace logos::hermes
