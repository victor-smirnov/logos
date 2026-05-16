// Logos project — https://github.com/victor-smirnov/logos
//
// ExternalRef: cross-arena reference handle for multi-arena IR.
// Phase 1.A of the multi-arena IR refactor.
//
// Layout in arena (8 bytes total):
//   byte -1:    TypeTag prefix (single byte, type_code = type_hash::ExternalRef
//               = 110; fits in the single-byte tag range 1-222 so the prefix
//               is exactly 1 byte).
//   byte  0-2:  arena_id (24-bit little-endian)
//   byte  3-6:  obj_id   (32-bit little-endian)
//
// Resolution dispatch (see docs/internals/multi-arena-ir.md §3.1):
//   pool[arena_id].directory[obj_id] → arena_offset → target.
//
// Q5 invariant (design doc): cross-arena refs use ONLY obj_id (never raw
// remote offsets). This makes ExternalRef the only legal cross-arena
// reference type and ensures all such references go through a directory
// indirection — analogous to bounds-checked array access vs raw pointers.

#pragma once

#include <cstdint>
#include <cstring>
#include <logos/hermes/arena_pool.hpp>   // arena_id_t
#include <logos/hermes/type_registry.hpp> // TypeTraits

namespace logos::hermes {

// ExternalRef payload (excluding the TypeTag prefix written immediately
// before this struct in the arena). Byte-aligned, no internal padding.
struct ExternalRef {
    // Raw bytes: [0..3) = arena_id LE, [3..7) = obj_id LE.
    // Stored as a flat byte array so the struct stays alignof = 1 and
    // can sit anywhere in the arena without padding concerns.
    uint8_t bytes[7];

    // --- Accessors ---

    arena_id_t arena_id() const noexcept {
        uint32_t v = uint32_t(bytes[0])
                   | (uint32_t(bytes[1]) << 8)
                   | (uint32_t(bytes[2]) << 16);
        return arena_id_t{v};
    }

    uint32_t obj_id() const noexcept {
        uint32_t v;
        std::memcpy(&v, bytes + 3, 4);
        return v;
    }

    // --- Mutators (used by writer helpers) ---

    void set_arena_id(arena_id_t aid) noexcept {
        bytes[0] = uint8_t(aid.value & 0xFF);
        bytes[1] = uint8_t((aid.value >> 8) & 0xFF);
        bytes[2] = uint8_t((aid.value >> 16) & 0xFF);
        // High byte of aid (if any) is silently dropped — caller is
        // responsible for aid <= MAX_ARENA_ID_VALUE.
    }

    void set_obj_id(uint32_t oid) noexcept {
        std::memcpy(bytes + 3, &oid, 4);
    }

    // --- Construction ---

    static ExternalRef make(arena_id_t aid, uint32_t oid) noexcept {
        ExternalRef r{};
        r.set_arena_id(aid);
        r.set_obj_id(oid);
        return r;
    }

    // --- Equality ---

    constexpr bool operator==(const ExternalRef& other) const noexcept {
        return std::memcmp(bytes, other.bytes, 7) == 0;
    }
    constexpr bool operator!=(const ExternalRef& other) const noexcept {
        return !(*this == other);
    }
};

static_assert(sizeof(ExternalRef)  == 7, "ExternalRef payload must be 7 bytes");
static_assert(alignof(ExternalRef) == 1, "ExternalRef must be byte-aligned");

// TypeTraits specialization — enables `arena_put<ExternalRef>(arena, value)`
// and `type_tag_for<ExternalRef>()`. Not embeddable (7 bytes > AnyVal's
// 3-byte inline payload), so it always lives in the arena and AnyVal holds
// a pointer-mode offset to it.
template <> struct TypeTraits<ExternalRef> {
    static constexpr uint64_t      hash        = type_hash::ExternalRef;
    static constexpr bool          fixed_size  = true;
    static constexpr bool          embeddable  = false;
    static constexpr TagDescriptor descriptor  = TagDescriptor::Data;
};

} // namespace logos::hermes
