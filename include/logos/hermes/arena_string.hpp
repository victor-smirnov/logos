// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string_view>

#include <logos/hermes/arena.hpp>
#include <logos/hermes/varint.hpp>
#include <logos/hermes/type_registry.hpp>
#include <logos/hermes/fnv_hash.hpp>
#include <logos/core/expected.hpp>

namespace logos::hermes {

// ArenaString: a UTF-8 string stored in the arena.
//
// Arena layout (immediately after the 2-byte TypeTag):
//   [vlen_u64_56 length] [UTF-8 data, no null terminator]
//
// This is a "data object" — its size is determined by the vlen-encoded length,
// not by sizeof(ArenaString). You don't construct ArenaString via placement new;
// instead, use ArenaString::create() which writes the raw bytes into the arena.
class ArenaString {
public:
    // Read the string from its arena location.
    // 'this' points to the first byte of the vlen-encoded length.
    std::string_view view() const noexcept {
        auto* self = reinterpret_cast<const uint8_t*>(this);
        VarIntResult len = varint_decode(self);
        return {reinterpret_cast<const char*>(self + len.bytes_read), len.value};
    }

    size_t length() const noexcept {
        auto* self = reinterpret_cast<const uint8_t*>(this);
        return varint_decode(self).value;
    }

    const char* data() const noexcept {
        auto* self = reinterpret_cast<const uint8_t*>(this);
        VarIntResult len = varint_decode(self);
        return reinterpret_cast<const char*>(self + len.bytes_read);
    }

    // Total bytes occupied in the arena (vlen header + string data).
    size_t arena_size() const noexcept {
        auto* self = reinterpret_cast<const uint8_t*>(this);
        VarIntResult len = varint_decode(self);
        return len.bytes_read + len.value;
    }

    uint64_t hash() const noexcept {
        auto sv = view();
        return fnv1a_hash(sv);
    }

    bool operator==(std::string_view other) const noexcept { return view() == other; }
    bool operator!=(std::string_view other) const noexcept { return view() != other; }

    // Allocate and write a string into the arena. Returns pointer to the ArenaString.
    [[nodiscard]] static logos::expected<ArenaString*> create(Arena& arena, std::string_view str) noexcept {
        // Calculate total size: vlen header + string bytes.
        uint8_t vlen_buf[8];
        size_t vlen_size = varint_encode(str.size(), vlen_buf);
        size_t total = vlen_size + str.size();

        TypeTag tag(type_hash::Varchar, TagDescriptor::Data);
        // Alignment 1 is fine for strings, but arena requires >= 2 for tag placement.
        LOGOS_TRY(auto* mem_void, arena.allocate(total, 2, tag));
        auto* dest = static_cast<uint8_t*>(mem_void);
        std::memcpy(dest, vlen_buf, vlen_size);
        std::memcpy(dest + vlen_size, str.data(), str.size());

        return reinterpret_cast<ArenaString*>(mem_void);
    }
};

} // namespace logos::hermes
