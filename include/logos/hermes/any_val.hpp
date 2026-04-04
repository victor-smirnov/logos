// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <logos/hermes/config.hpp>
#include <logos/hermes/type_registry.hpp>
#include <logos/hermes/named_code.hpp>

namespace logos::hermes {

// AnyVal: an 8-byte polymorphic slot that holds either a segment-relative
// offset to an arena object, or a small value embedded inline (up to 7 bytes).
//
// Layout (little-endian uint64_t):
//
// Pointer mode (discriminant bit = 0):
//   bits [0:31]  = arena_offset_t offset (segment-relative)
//   bits [32:62] = reserved (zero)
//   bit  63      = 0 (discriminant)
//   Null: all bits zero (offset 0 is DocumentHeader, but AnyVal never points there).
//   Actually null is bits_ == 0 which means offset=0; we use NULL_OFFSET for real null.
//
// Value mode (discriminant bit = 1):
//   bytes [0..6] = value data (up to 7 bytes, zero-padded)
//   byte  [7]    = (type_hash << 1) | 1
//
// With segment-relative offsets, AnyVal can be freely copied between
// memory locations without relocation — the offset is from the segment
// base, not from the AnyVal's own address.
class AnyVal {
public:
    AnyVal() noexcept : bits_(0) {}

    // --- Discriminant ---

    bool is_null() const noexcept { return bits_ == 0; }
    bool is_pointer() const noexcept { return !is_null() && (last_byte() & 1) == 0; }
    bool is_value() const noexcept { return (last_byte() & 1) == 1; }

    // --- Pointer mode (segment-relative offset) ---

    // Create a AnyVal in pointer mode from a segment-relative offset.
    static AnyVal from_offset(arena_offset_t offset) noexcept {
        AnyVal p;
        p.bits_ = static_cast<uint64_t>(offset.value());
        return p;
    }

    // Recover the segment-relative offset.
    arena_offset_t to_offset() const noexcept {
        return arena_offset_t{static_cast<arena_offset_t::value_type>(bits_)};
    }

    // Dereference: requires segment base address.
    template <typename T>
    T* as_ptr(uint8_t* base) const noexcept {
        return reinterpret_cast<T*>(base + to_offset().value());
    }

    template <typename T>
    const T* as_ptr(const uint8_t* base) const noexcept {
        return reinterpret_cast<const T*>(base + to_offset().value());
    }

    // Set this AnyVal to point at target (pointer mode), given segment base.
    void set_pointer(const void* target, const uint8_t* base) noexcept {
        auto offset = arena_offset_t{static_cast<arena_offset_t::value_type>(
            static_cast<const uint8_t*>(target) - base)};
        *this = from_offset(offset);
    }

    // Set from a known offset.
    void set_offset(arena_offset_t offset) noexcept {
        *this = from_offset(offset);
    }

    // --- Value mode ---

    // Embed a small value with a type hash tag.
    template <typename T>
    static AnyVal from_value(T value, uint8_t type_hash) noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(sizeof(T) <= 7);

        AnyVal p;
        p.bits_ = 0;
        std::memcpy(&p.bits_, &value, sizeof(T));
        auto* bytes = reinterpret_cast<uint8_t*>(&p.bits_);
        bytes[7] = static_cast<uint8_t>((type_hash << 1) | 1);
        return p;
    }

    // Convenience: deduce type_hash from TypeTraits — no need to spell it out.
    //   AnyVal::from_value(int32_t(7))   instead of
    //   AnyVal::from_value(int32_t(7), type_hash::Integer)
    template <typename T>
        requires requires { requires TypeTraits<T>::embeddable; } && (sizeof(T) <= 7)
    static AnyVal from_value(T value) noexcept {
        return from_value(value, static_cast<uint8_t>(TypeTraits<T>::hash));
    }

    // NamedCode<T> overload: template deduction doesn't apply implicit conversions,
    // so NamedCode<int32_t> wouldn't match T above without this.
    template <typename T>
        requires requires { requires TypeTraits<T>::embeddable; } && (sizeof(T) <= 7)
    static AnyVal from_value(NamedCode<T> value) noexcept {
        return from_value(value.code, static_cast<uint8_t>(TypeTraits<T>::hash));
    }

    // Extract the embedded value.
    template <typename T>
    T as_value() const noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(sizeof(T) <= 7);

        T result{};
        std::memcpy(&result, &bits_, sizeof(T));
        return result;
    }

    // Extract the 7-bit type hash from the tag byte.
    uint8_t value_type_hash() const noexcept {
        return last_byte() >> 1;
    }

    // --- Raw access ---

    uint64_t raw() const noexcept { return bits_; }
    static AnyVal from_raw(uint64_t bits) noexcept { AnyVal p; p.bits_ = bits; return p; }

private:
    uint64_t bits_;

    uint8_t last_byte() const noexcept {
        auto* bytes = reinterpret_cast<const uint8_t*>(&bits_);
        return bytes[7];
    }
};

static_assert(sizeof(AnyVal) == 8);

} // namespace logos::hermes
