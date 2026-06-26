// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string_view>

#include <logos/writ/arena.hpp>
#include <logos/writ/varint.hpp>
#include <logos/writ/type_codes.hpp>
#include <logos/writ/fnv_hash.hpp>
#include <logos/core/expected.hpp>

namespace logos::writ {

// ArenaString — a UTF-8 string in the arena. BYTE-IDENTICAL to the Logos stdlib
// HString (stdlib/lang/writ2/hstring.logos): a self-describing object whose whole
// body is `[ vlen(payload_len) ][ UTF-8 payload ]` (no sized prefix, no separate
// length field). Tag = tc::STRING. Ported verbatim from Writ1's ArenaString
// (which has the same layout; only the tag code changed 28 → 130).
class ArenaString {
public:
    // 'this' points at the first byte of the vlen-encoded length.
    std::string_view view() const noexcept {
        auto* self = reinterpret_cast<const uint8_t*>(this);
        VarIntResult len = varint_decode(self);
        return {reinterpret_cast<const char*>(self + len.bytes_read), len.value};
    }

    size_t length() const noexcept {
        return varint_decode(reinterpret_cast<const uint8_t*>(this)).value;
    }

    const char* data() const noexcept {
        auto* self = reinterpret_cast<const uint8_t*>(this);
        VarIntResult len = varint_decode(self);
        return reinterpret_cast<const char*>(self + len.bytes_read);
    }

    // Total bytes occupied in the arena (vlen header + payload).
    size_t arena_size() const noexcept {
        auto* self = reinterpret_cast<const uint8_t*>(this);
        VarIntResult len = varint_decode(self);
        return len.bytes_read + len.value;
    }

    uint64_t hash() const noexcept { return fnv1a_hash(view()); }

    bool operator==(std::string_view other) const noexcept { return view() == other; }
    bool operator!=(std::string_view other) const noexcept { return view() != other; }

    // Allocate + write a string into the arena. Returns the ArenaString pointer.
    [[nodiscard]] static logos::expected<ArenaString*>
    create(Arena& arena, std::string_view str) noexcept {
        uint8_t vlen_buf[8];
        size_t vlen_size = varint_encode(str.size(), vlen_buf);
        size_t total = vlen_size + str.size();

        TypeTag tag(tc::STRING);
        LOGOS_TRY(auto* mem_void, arena.allocate(total, 2, tag));
        auto* dest = static_cast<uint8_t*>(mem_void);
        std::memcpy(dest, vlen_buf, vlen_size);
        std::memcpy(dest + vlen_size, str.data(), str.size());
        return reinterpret_cast<ArenaString*>(mem_void);
    }
};

} // namespace logos::writ
