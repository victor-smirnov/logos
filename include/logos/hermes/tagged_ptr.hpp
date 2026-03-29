// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <logos/hermes/config.hpp>

namespace logos::hermes {

// TaggedPtr: an 8-byte polymorphic slot that holds either a segment-relative
// offset to an arena object, or a small value embedded inline (up to 7 bytes).
//
// Layout (little-endian uint64_t):
//
// Pointer mode (discriminant bit = 0):
//   bits [0:31]  = arena_offset_t offset (segment-relative)
//   bits [32:62] = reserved (zero)
//   bit  63      = 0 (discriminant)
//   Null: all bits zero (offset 0 is DocumentHeader, but TaggedPtr never points there).
//   Actually null is bits_ == 0 which means offset=0; we use NULL_OFFSET for real null.
//
// Value mode (discriminant bit = 1):
//   bytes [0..6] = value data (up to 7 bytes, zero-padded)
//   byte  [7]    = (type_hash << 1) | 1
//
// With segment-relative offsets, TaggedPtr can be freely copied between
// memory locations without relocation — the offset is from the segment
// base, not from the TaggedPtr's own address.
class TaggedPtr {
public:
    TaggedPtr() : bits_(0) {}

    // --- Discriminant ---

    bool is_null() const { return bits_ == 0; }
    bool is_pointer() const { return !is_null() && (last_byte() & 1) == 0; }
    bool is_value() const { return (last_byte() & 1) == 1; }

    // --- Pointer mode (segment-relative offset) ---

    // Create a TaggedPtr in pointer mode from a segment-relative offset.
    static TaggedPtr from_offset(arena_offset_t offset) {
        TaggedPtr p;
        p.bits_ = static_cast<uint64_t>(offset);
        return p;
    }

    // Recover the segment-relative offset.
    arena_offset_t to_offset() const {
        return static_cast<arena_offset_t>(bits_);
    }

    // Dereference: requires segment base address.
    template <typename T>
    T* as_ptr(uint8_t* base) const {
        return reinterpret_cast<T*>(base + to_offset());
    }

    template <typename T>
    const T* as_ptr(const uint8_t* base) const {
        return reinterpret_cast<const T*>(base + to_offset());
    }

    // Set this TaggedPtr to point at target (pointer mode), given segment base.
    void set_pointer(const void* target, const uint8_t* base) {
        auto offset = static_cast<arena_offset_t>(
            static_cast<const uint8_t*>(target) - base);
        *this = from_offset(offset);
    }

    // Set from a known offset.
    void set_offset(arena_offset_t offset) {
        *this = from_offset(offset);
    }

    // --- Value mode ---

    // Embed a small value with a type hash tag.
    template <typename T>
    static TaggedPtr from_value(T value, uint8_t type_hash) {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(sizeof(T) <= 7);

        TaggedPtr p;
        p.bits_ = 0;
        std::memcpy(&p.bits_, &value, sizeof(T));
        auto* bytes = reinterpret_cast<uint8_t*>(&p.bits_);
        bytes[7] = static_cast<uint8_t>((type_hash << 1) | 1);
        return p;
    }

    // Extract the embedded value.
    template <typename T>
    T as_value() const {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(sizeof(T) <= 7);

        T result{};
        std::memcpy(&result, &bits_, sizeof(T));
        return result;
    }

    // Extract the 7-bit type hash from the tag byte.
    uint8_t value_type_hash() const {
        return last_byte() >> 1;
    }

    // --- Raw access ---

    uint64_t raw() const { return bits_; }
    static TaggedPtr from_raw(uint64_t bits) { TaggedPtr p; p.bits_ = bits; return p; }

private:
    uint64_t bits_;

    uint8_t last_byte() const {
        auto* bytes = reinterpret_cast<const uint8_t*>(&bits_);
        return bytes[7];
    }
};

static_assert(sizeof(TaggedPtr) == 8);

} // namespace logos::hermes
