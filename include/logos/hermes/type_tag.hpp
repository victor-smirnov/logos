// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <cstddef>

namespace logos::hermes {

// What kind of arena object a TypeTag describes.
enum class TagDescriptor : uint8_t {
    Data  = 0,  // Plain data type (integers, strings, etc.)
    Array = 1,  // ObjectArray or TypedArray
    Map   = 2,  // TinyObjectMap or ObjectMap
};

// TypeTag: variable-length type identifier stored in the arena bytes
// immediately before each object.
//
// Encoded as a little-endian uint64_t with bit fields:
//   bits [2:0]  = code_len   (0-7, number of additional bytes beyond the first)
//   bits [7:3]  = descriptor (0-31, see TagDescriptor)
//   bits [63:8] = type_code  (56-bit type hash)
//
// Total byte length = code_len + 1 (range: 1 to 8 bytes).
// The tag is written backwards from the object's start address.
class TypeTag {
public:
    constexpr TypeTag() : raw_(0) {}

    // Construct from a type hash and descriptor.
    // code_len is computed automatically from the type_hash magnitude.
    constexpr TypeTag(uint64_t type_hash, TagDescriptor descriptor = TagDescriptor::Data)
        : raw_(encode(type_hash, descriptor)) {}

    // --- Field accessors ---

    constexpr uint8_t code_len() const { return raw_ & 0x07; }
    constexpr size_t  byte_length() const { return code_len() + 1; }
    constexpr TagDescriptor descriptor() const {
        return static_cast<TagDescriptor>((raw_ >> 3) & 0x1F);
    }
    constexpr uint64_t type_code() const { return raw_ >> 8; }
    constexpr uint64_t raw() const { return raw_; }

    constexpr bool operator==(const TypeTag& other) const { return raw_ == other.raw_; }
    constexpr bool operator!=(const TypeTag& other) const { return raw_ != other.raw_; }

    // --- Arena I/O ---

    // Write this tag into the bytes immediately before object_addr.
    // Caller must ensure at least byte_length() bytes are available before object_addr.
    void write_before(uint8_t* object_addr) const {
        size_t len = byte_length();
        for (size_t i = 0; i < len; ++i) {
            object_addr[-(ptrdiff_t)(i + 1)] = static_cast<uint8_t>(raw_ >> (i * 8));
        }
    }

    // Read a tag from the bytes immediately before object_addr.
    static TypeTag read_before(const uint8_t* object_addr) {
        uint8_t first_byte = object_addr[-1];
        uint8_t code_len = first_byte & 0x07;
        uint64_t val = 0;
        for (size_t i = 0; i <= code_len; ++i) {
            val |= static_cast<uint64_t>(object_addr[-(ptrdiff_t)(i + 1)]) << (i * 8);
        }
        TypeTag tag;
        tag.raw_ = val;
        return tag;
    }

    // Construct from a raw uint64_t value (used by binary decoder).
    static constexpr TypeTag from_raw(uint64_t raw) {
        TypeTag tag;
        tag.raw_ = raw;
        return tag;
    }

private:
    uint64_t raw_;

    // Determine the minimum code_len needed to represent type_hash in the upper bits.
    static constexpr uint8_t needed_code_len(uint64_t type_hash) {
        if (type_hash == 0) return 0;
        // type_hash occupies bits [63:8], so we need enough bytes to hold (type_hash << 8).
        // code_len = (total_bytes - 1), total_bytes = ceil_byte_count of the full encoded value.
        uint64_t full = type_hash << 8; // the upper portion of the encoded value
        uint8_t bytes_needed = 1;
        while ((full >> (bytes_needed * 8)) != 0) {
            ++bytes_needed;
        }
        return bytes_needed - 1; // code_len = additional bytes beyond the first
    }

    static constexpr uint64_t encode(uint64_t type_hash, TagDescriptor descriptor) {
        uint8_t cl = needed_code_len(type_hash);
        return (type_hash << 8)
             | (static_cast<uint64_t>(descriptor) << 3)
             | cl;
    }
};

static_assert(sizeof(TypeTag) == 8);

} // namespace logos::hermes
