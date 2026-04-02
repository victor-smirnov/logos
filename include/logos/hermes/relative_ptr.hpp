// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <logos/hermes/config.hpp>

namespace logos::hermes {

// RelativePtr<T>: segment-relative pointer.
//
// Stores an offset from the segment base (not from its own address).
// All dereferences require the segment base pointer to be passed explicitly.
// This makes the pointer stable across realloc (offset doesn't change when
// the segment moves) and eliminates cross-chunk pointer issues.
//
// Null is represented by NULL_OFFSET.
template <typename T>
class RelativePtr {
public:
    RelativePtr() : offset_(NULL_OFFSET) {}
    explicit RelativePtr(arena_offset_t offset) : offset_(offset) {}

    bool is_null() const { return offset_ == NULL_OFFSET; }

    // Dereference: requires segment base address.
    T* get(uint8_t* base) const {
        if (offset_ == NULL_OFFSET) return nullptr;
        return reinterpret_cast<T*>(base + offset_.value());
    }

    const T* get(const uint8_t* base) const {
        if (offset_ == NULL_OFFSET) return nullptr;
        return reinterpret_cast<const T*>(base + offset_.value());
    }

    template <typename U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    U& deref(uint8_t* base) const { return *get(base); }

    template <typename U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    const U& deref(const uint8_t* base) const { return *get(base); }

    // Set from a raw pointer + base.
    void set(const void* target, const uint8_t* base) {
        if (!target) { offset_ = NULL_OFFSET; return; }
        offset_ = arena_offset_t{static_cast<arena_offset_t::value_type>(
            static_cast<const uint8_t*>(target) - base)};
    }

    // Set directly from an offset.
    void set_offset(arena_offset_t off) { offset_ = off; }

    void clear() { offset_ = NULL_OFFSET; }

    arena_offset_t offset() const { return offset_; }

private:
    arena_offset_t offset_;
};

static_assert(sizeof(RelativePtr<int>) == sizeof(arena_offset_t));

} // namespace logos::hermes
