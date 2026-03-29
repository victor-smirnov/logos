// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <cstddef>
#include <logos/hermes/arena.hpp>

namespace logos::hermes {

inline constexpr size_t ARENA_NULL = ~size_t(0);

// ArenaPtr<T>: an offset-based handle to an arena-allocated object.
//
// Stores the byte offset from the arena start. To access the object,
// call get(arena). This survives arena realloc — the offset is stable
// even when the arena's base address changes.
//
// This is the external API complement to RelativePtr (which is internal,
// stored inside the arena, and encodes offsets from its own address).
template <typename T>
class ArenaPtr {
public:
    ArenaPtr() : offset_(ARENA_NULL) {}
    explicit ArenaPtr(size_t offset) : offset_(offset) {}

    bool is_null() const { return offset_ == ARENA_NULL; }
    size_t offset() const { return offset_; }

    T* get(Arena& arena) const {
        if (offset_ == ARENA_NULL) return nullptr;
        return reinterpret_cast<T*>(arena.data() + offset_);
    }

    const T* get(const Arena& arena) const {
        if (offset_ == ARENA_NULL) return nullptr;
        return reinterpret_cast<const T*>(arena.data() + offset_);
    }

    // Convenience: dereference with arena (same as get, shorter name).
    T* of(Arena& arena) const { return get(arena); }
    const T* of(const Arena& arena) const { return get(arena); }

    // Construct from a raw pointer + arena (compute offset).
    static ArenaPtr from_ptr(const void* ptr, const Arena& arena) {
        if (!ptr) return ArenaPtr{};
        size_t off = static_cast<const uint8_t*>(ptr) - arena.data();
        return ArenaPtr(off);
    }

    // Convert to raw void* offset (for interop with code that uses size_t offsets).
    operator size_t() const { return offset_; }

    bool operator==(const ArenaPtr& other) const { return offset_ == other.offset_; }
    bool operator!=(const ArenaPtr& other) const { return offset_ != other.offset_; }

private:
    size_t offset_;
};

} // namespace logos::hermes
