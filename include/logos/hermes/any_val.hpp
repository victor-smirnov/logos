// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <logos/hermes/config.hpp>
#include <logos/hermes/type_registry.hpp>
#include <logos/hermes/named_code.hpp>

namespace logos::hermes {

// AnyVal: a 4-byte polymorphic slot that holds either a segment-relative
// offset to an arena object, or a small value embedded inline (up to 24 bits).
//
// Layout (little-endian uint32_t):
//
// Pointer mode (discriminant bit 0 = 0):
//   bits [31:0] = arena_offset_t offset (segment-relative)
//   Required: offsets must be even so bit 0 stays 0; this is guaranteed
//   because every arena allocation aligns to ≥2 bytes.
//   Null: raw == 0 (offset 0 is DocumentHeader, never a data object).
//
// Value mode (discriminant bit 0 = 1):
//   bits [0]    = 1 (discriminant)
//   bits [7:1]  = type_hash (7 bits, 0–127)
//   bits [31:8] = 24-bit value payload
//
// Embeddable Hermes types (Logos-aligned):
//   TinyInt   i8   hash=20
//   UTinyInt  u8   hash=21
//   SmallInt  i16  hash=22
//   Integer   i32  hash=23  (only values fitting in 24 bits — else zone ptr)
//   USmallInt u16  hash=24
//   UInteger  u32  hash=25  (only values fitting in 24 bits — else zone ptr)
//   Boolean   u8   hash=37
//
// NOT embeddable: i64 / u64 / f32 / f64 / timestamps / strings / decimals.
// These always live in the arena and AnyVal holds an offset to them.
//
// Byte-for-byte compatible with stdlib/hermes/anyval.logos.
class AnyVal {
public:
    AnyVal() noexcept : bits_(0) {}

    // --- Discriminant ---

    bool is_null()    const noexcept { return bits_ == 0; }
    bool is_pointer() const noexcept { return !is_null() && (bits_ & 1u) == 0u; }
    bool is_value()   const noexcept { return (bits_ & 1u) == 1u; }

    // --- Pointer mode (segment-relative offset) ---

    static AnyVal from_offset(arena_offset_t offset) noexcept {
        AnyVal p;
        p.bits_ = offset.value();
        return p;
    }

    arena_offset_t to_offset() const noexcept {
        return arena_offset_t{bits_};
    }

    template <typename T>
    T* as_ptr(uint8_t* base) const noexcept {
        return reinterpret_cast<T*>(base + bits_);
    }

    template <typename T>
    const T* as_ptr(const uint8_t* base) const noexcept {
        return reinterpret_cast<const T*>(base + bits_);
    }

    void set_pointer(const void* target, const uint8_t* base) noexcept {
        auto off = static_cast<uint32_t>(
            static_cast<const uint8_t*>(target) - base);
        bits_ = off;
    }

    void set_offset(arena_offset_t offset) noexcept {
        bits_ = offset.value();
    }

    // --- Value mode ---

    // Embed a small value with a type hash tag. Value must fit in 24 bits.
    template <typename T>
    static AnyVal from_value(T value, uint8_t type_hash) noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(sizeof(T) <= 3 || std::is_integral_v<T>,
            "from_value: value must fit in 24 bits (integer or sizeof ≤ 3)");

        uint32_t payload = 0;
        if constexpr (sizeof(T) <= 3) {
            std::memcpy(&payload, &value, sizeof(T));
            // Sign-extend narrow signed types into the 24-bit slot so the
            // encoded payload's upper bits mirror Logos' embed_i8/i16 layout.
            if constexpr (std::is_signed_v<T>) {
                if constexpr (sizeof(T) == 1) {
                    if (static_cast<int8_t>(payload) < 0) payload |= 0xFFFFFF00u;
                } else if constexpr (sizeof(T) == 2) {
                    if (static_cast<int16_t>(static_cast<uint16_t>(payload)) < 0)
                        payload |= 0xFFFF0000u;
                }
            }
        } else {
            // 4-byte integer — must fit in 24 bits (signed range -(2^23) .. 2^23-1,
            // unsigned 0 .. 2^24-1).
            std::memcpy(&payload, &value, 4);
        }
        payload &= 0x00FFFFFFu;
        AnyVal p;
        p.bits_ = (payload << 8) | (static_cast<uint32_t>(type_hash) << 1) | 1u;
        return p;
    }

    // Convenience: deduce type_hash from TypeTraits.
    template <typename T>
        requires requires { requires TypeTraits<T>::embeddable; }
    static AnyVal from_value(T value) noexcept {
        return from_value(value, static_cast<uint8_t>(TypeTraits<T>::hash));
    }

    // NamedCode<T> overload.
    template <typename T>
        requires requires { requires TypeTraits<T>::embeddable; }
    static AnyVal from_value(NamedCode<T> value) noexcept {
        return from_value(value.code, static_cast<uint8_t>(TypeTraits<T>::hash));
    }

    // Extract the embedded value (sign-extends for signed integer T).
    template <typename T>
    T as_value() const noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(sizeof(T) <= 4);

        uint32_t payload = (bits_ >> 8) & 0x00FFFFFFu;
        if constexpr (std::is_signed_v<T> && std::is_integral_v<T>) {
            // Sign-extend from 24 bits.
            if (payload & 0x00800000u) payload |= 0xFF000000u;
        }
        T result{};
        if constexpr (sizeof(T) <= 3) {
            std::memcpy(&result, &payload, sizeof(T));
        } else {
            std::memcpy(&result, &payload, 4);
        }
        return result;
    }

    // Extract the 7-bit type hash from the tag byte.
    uint8_t value_type_hash() const noexcept {
        return static_cast<uint8_t>((bits_ >> 1) & 0x7Fu);
    }

    // --- Raw access ---

    uint32_t raw() const noexcept { return bits_; }
    static AnyVal from_raw(uint32_t bits) noexcept {
        AnyVal p; p.bits_ = bits; return p;
    }

private:
    uint32_t bits_;
};

static_assert(sizeof(AnyVal) == 4);

} // namespace logos::hermes
