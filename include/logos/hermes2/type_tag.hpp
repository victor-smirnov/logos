// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <cstddef>

namespace logos::hermes2 {

// What kind of arena object a TypeTag describes.
enum class TagDescriptor : uint8_t {
    Data  = 0,  // Plain data type (integers, strings, etc.)
    Array = 1,  // ObjectArray or TypedArray
    Map   = 2,  // TinyObjectMap or ObjectMap
};

// TypeTag: variable-length type identifier stored in the arena bytes
// immediately before each object.
//
// Wire format (byte-for-byte compatible with stdlib/hermes/datatag.logos):
//
//   obj[-1] == 0                : unset / sentinel (type_code 0, 0 bytes)
//   obj[-1] ∈ [1, 222]          : single-byte tag; type_code == obj[-1],
//                                 total length == 1 byte
//   obj[-1] ∈ [223, 230]        : multi-byte tag; n_code_bytes == obj[-1] - 222,
//                                 obj[-2]..obj[-1 - n_code_bytes] hold the
//                                 little-endian code bytes, total length ==
//                                 1 + n_code_bytes
//
// `TagDescriptor` is no longer encoded in the wire — the type_code alone
// identifies the object class.  `descriptor()` derives the category from the
// type_code via a small table so call sites that ask "is this an array?" keep
// working unchanged.
class TypeTag {
public:
    constexpr TypeTag() : type_code_(0) {}

    // Construct from a type code.  `descriptor` is accepted for API
    // compatibility with older call sites but is ignored — the descriptor is
    // derived from the type_code at read time.
    constexpr TypeTag(uint64_t type_code, TagDescriptor /*unused*/ = TagDescriptor::Data)
        : type_code_(type_code) {}

    // --- Field accessors ---

    // Number of additional bytes beyond the first (kept for API compat).
    // code_len() + 1 == byte_length().
    constexpr uint8_t code_len() const noexcept {
        size_t bl = byte_length();
        return bl == 0 ? 0 : static_cast<uint8_t>(bl - 1);
    }

    // Byte length of the tag in the arena (0, 1, or 2..8).
    constexpr size_t byte_length() const noexcept {
        if (type_code_ == 0) return 0;
        if (type_code_ <= 222) return 1;
        // Multi-byte: header byte + ceil_log256(type_code) code bytes.
        size_t n = 0;
        uint64_t v = type_code_;
        while (v > 0) { v >>= 8; ++n; }
        if (n > 8) n = 8;
        return 1 + n;
    }

    // Descriptor derived from type_code — no longer carried on the wire.
    // Values mirror type_hash::* in type_registry.hpp (duplicated here to
    // avoid a circular include; type_registry.hpp includes this file).
    constexpr TagDescriptor descriptor() const noexcept {
        switch (type_code_) {
            // Maps
            case 98:   // Hermes / TinyObjectMap
            case 101:  // ObjectMap
            case 3101: // MapI32AnyVal
            case 3102: // MapU32AnyVal
            case 3103: // MapI64AnyVal
            case 3104: // MapU64AnyVal
                return TagDescriptor::Map;
            // Arrays
            case 100:  // ObjectArray
            case 2101: // ArrayU8
            case 2102: // ArrayU16
            case 2103: // ArrayU32
            case 2104: // ArrayU64
            case 2105: // ArrayI8
            case 2106: // ArrayI16
            case 2107: // ArrayI32
            case 2108: // ArrayI64
            case 2109: // ArrayF32
            case 2110: // ArrayF64
                return TagDescriptor::Array;
            // Everything else (scalars, strings, Decimal=102, Datatype=1001,
            // TypedValue=4115, …) is Data.
            default:
                return TagDescriptor::Data;
        }
    }

    constexpr uint64_t type_code() const noexcept { return type_code_; }
    constexpr uint64_t raw() const noexcept { return type_code_; }

    constexpr bool operator==(const TypeTag& other) const noexcept { return type_code_ == other.type_code_; }
    constexpr bool operator!=(const TypeTag& other) const noexcept { return type_code_ != other.type_code_; }

    // --- Arena I/O ---

    // Write this tag into the bytes immediately before object_addr.
    // Caller must ensure at least byte_length() bytes are available before object_addr.
    void write_before(uint8_t* object_addr) const noexcept {
        if (type_code_ == 0) return;
        if (type_code_ <= 222) {
            object_addr[-1] = static_cast<uint8_t>(type_code_);
            return;
        }
        // Multi-byte: write code bytes little-endian starting at obj[-2],
        // then the header byte (223 + (n_code_bytes - 1)) at obj[-1].
        uint64_t v = type_code_;
        size_t   i = 0;
        while (v > 0) {
            object_addr[-2 - static_cast<ptrdiff_t>(i)] = static_cast<uint8_t>(v & 0xFF);
            v >>= 8;
            ++i;
        }
        if (i > 8) i = 8;
        object_addr[-1] = static_cast<uint8_t>(223 + (i - 1));
    }

    // Read a tag from the bytes immediately before object_addr.
    static TypeTag read_before(const uint8_t* object_addr) noexcept {
        uint8_t b = object_addr[-1];
        if (b == 0)                return TypeTag{};
        if (b >= 1 && b <= 222)    { TypeTag t; t.type_code_ = b; return t; }
        // b > 230 would require >8 code bytes — out of range; treat as unset.
        if (b > 230)               return TypeTag{};
        size_t   n    = static_cast<size_t>(b - 223 + 1);
        uint64_t code = 0;
        for (size_t i = 0; i < n; ++i) {
            uint64_t byte = object_addr[-2 - static_cast<ptrdiff_t>(i)];
            code |= byte << (i * 8);
        }
        TypeTag t;
        t.type_code_ = code;
        return t;
    }

    // Construct from a raw value (kept for codec-level interop).
    // In this encoding "raw" is just the numeric type_code.
    static constexpr TypeTag from_raw(uint64_t raw) noexcept {
        TypeTag tag;
        tag.type_code_ = raw;
        return tag;
    }

private:
    uint64_t type_code_;
};

static_assert(sizeof(TypeTag) == 8);

} // namespace logos::hermes2
