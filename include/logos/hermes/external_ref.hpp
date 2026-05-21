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
#include <optional>
#include <logos/hermes/arena_pool.hpp>   // arena_id_t
#include <logos/hermes/any_val.hpp>      // AnyVal
#include <logos/hermes/type_tag.hpp>     // TypeTag (for is_external_ref_av)
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

// ── Phase 2.A: detection + resolution helpers ────────────────────────────
//
// These let view-layer / consumer code work with ExternalRef WITHOUT yet
// changing RefBase / TypeRef shape. The full multi-arena refactor of
// RefBase / TypeRef (Phase 2.B) will use the same helper logic inside
// sub_expr() / sub_type() etc.

// Quick check: is `av` a pointer-mode AnyVal whose target's TypeTag prefix
// is type_hash::ExternalRef? Returns false for null / value-mode AnyVal /
// pointer-to-other-type.
//
// `base` must be the arena base where `av`'s offset is rooted.
inline bool is_external_ref_av(AnyVal av, const uint8_t* base) noexcept {
    if (!av.is_pointer()) return false;
    auto* obj = base + av.to_offset().value();
    auto tag = TypeTag::read_before(obj);
    return tag.type_code() == type_hash::ExternalRef;
}

// Resolution result: arena (via MemHolder) + offset of the target object
// within that arena. Caller dereferences as needed via mem->base() + offset.
struct ExternalRefResolved {
    MemHolder*             mem;
    arena_offset_t         offset;

    constexpr bool ok() const noexcept { return mem != nullptr; }
};

// Resolve via ArenaPool dispatch. See docs/internals/multi-arena-ir.md §3.1
// for the 3-indirection model (pool index → directory ptr → directory
// lookup → target offset).
//
// Returns ok()=false if arena_id isn't registered in the pool or obj_id is
// out of range. obj_id 0 is INVALID per invariant #13 → ok()=false.
ExternalRefResolved resolve_external_ref(
    const ExternalRef& ref,
    ArenaPool&         pool = global_arena_pool()) noexcept;

// Resolve an ExternalRef whose arena_id is MODULE-LOCAL (an index into
// `source_arena`'s import table) rather than a global arena_id. Translates
// the local arena_id → global arena_id via pool.resolve_local_arena_id(...)
// (which walks source_arena's import entry → file_name → loaded document),
// then resolves the obj_id in that document's directory. ok()=false if the
// local arena_id can't be resolved (no import entry / file not loaded).
//
// `source_arena` is the global arena_id of the document that CONTAINS `ref`.
ExternalRefResolved resolve_external_ref_local(
    arena_id_t         source_arena,
    const ExternalRef& ref,
    ArenaPool&         pool = global_arena_pool()) noexcept;

// Convenience: detect + resolve in one go. Returns nullopt if `av` is not
// a pointer to ExternalRef (use this when walking a generic AnyVal slot
// and you want "transparent" cross-arena traversal).
inline std::optional<ExternalRefResolved> resolve_if_external(
    AnyVal             av,
    const uint8_t*     base,
    ArenaPool&         pool = global_arena_pool()) noexcept
{
    if (!is_external_ref_av(av, base)) return std::nullopt;
    auto* ref = reinterpret_cast<const ExternalRef*>(
        base + av.to_offset().value());
    auto r = resolve_external_ref(*ref, pool);
    if (!r.ok()) return std::nullopt;
    return r;
}

} // namespace logos::hermes
