// Logos project — https://github.com/victor-smirnov/logos
//
// ExternalRef (Hermes2) — the cross-arena reference handle for the multi-arena IR.
//
// HERMES2 CHANGE vs Hermes1: an ExternalRef is now an AnyVal **Pod niche**, not a
// separately-allocated 7-byte tagged arena object. The 8-byte self-relative AnyVal
// Pod arm carries a 56-bit value — EXACTLY arena_id(24) + obj_id(32). So the whole
// reference fits inline with NO allocation:
//
//   AnyVal::pod( (arena_id << 32) | obj_id , EXTERNAL_REF_CODE )
//
// This is strictly better than the Hermes1 tagged-object form (which only existed
// because Hermes1's 4-byte AnyVal had a mere 3-byte inline payload):
//   • no arena object to allocate / address;
//   • clone/compactify copy Pods VERBATIM, which is exactly right for a cross-arena
//     reference (it is a LOGICAL id, not a physical pointer — it must NOT be
//     followed/rewritten by the deep-copy, and a Pod never is);
//   • resolution is a direct decode, no in-arena read.
//
// Q5 invariant: cross-arena refs carry ONLY (arena_id, obj_id) — never a raw remote
// pointer — so every cross-arena reference is forced through the pool + directory
// indirection (bounds-checked, like array access vs raw pointers).

#pragma once

#include <cstdint>
#include <optional>

#include <logos/hermes2/any_val.hpp>
#include <logos/hermes2/arena_pool.hpp>   // arena_id_t, ArenaPool, global_arena_pool

namespace logos::hermes2 {

class MemHolder;

// The Pod type code identifying an ExternalRef niche (7-bit; matches the Hermes1
// type_hash::ExternalRef number for continuity). Distinct from every scalar Pod code.
inline constexpr uint8_t EXTERNAL_REF_CODE = 110;

// Decoded cross-arena reference value (arena_id is module-local OR global depending
// on the resolve entry point).
struct ExternalRef {
    arena_id_t aid;
    uint32_t   oid = 0;

    static ExternalRef make(arena_id_t a, uint32_t o) noexcept { return ExternalRef{a, o}; }

    constexpr bool operator==(const ExternalRef&) const noexcept = default;
};

// Encode (arena_id, obj_id) into an AnyVal Pod niche (no allocation).
inline AnyVal external_ref_av(arena_id_t aid, uint32_t oid) noexcept {
    uint64_t v = (static_cast<uint64_t>(aid.value & MAX_ARENA_ID_VALUE) << 32)
               |  static_cast<uint64_t>(oid);
    return AnyVal::pod(static_cast<int64_t>(v), EXTERNAL_REF_CODE);
}
inline AnyVal external_ref_av(const ExternalRef& r) noexcept {
    return external_ref_av(r.aid, r.oid);
}

// Is `av` an ExternalRef Pod niche?
inline bool is_external_ref_av(AnyVal av) noexcept {
    return av.is_pod() && av.pod_code() == EXTERNAL_REF_CODE;
}

// Decode an ExternalRef Pod niche (caller checks is_external_ref_av first).
inline ExternalRef decode_external_ref(AnyVal av) noexcept {
    uint64_t v = (static_cast<uint64_t>(av.raw()) >> 8) & 0x00FFFFFFFFFFFFFFull;  // 56-bit value
    return ExternalRef{arena_id_t{static_cast<uint32_t>(v >> 32)},
                       static_cast<uint32_t>(v & 0xFFFFFFFFull)};
}

// Resolution result: the owning arena (MemHolder, NOT +ref'd — borrowed via the
// pool which keeps it alive) + the absolute pointer to the target object within it.
struct ExternalRefResolved {
    MemHolder*     mem = nullptr;
    const uint8_t* obj = nullptr;

    constexpr bool ok() const noexcept { return mem != nullptr; }
};

// Resolve a GLOBAL-arena-id ExternalRef via pool dispatch: arena_id → MemHolder →
// LirArenaRoot → DIRECTORY[obj_id] → target object. ok()=false if the arena isn't
// registered, the obj_id is out of range, or obj_id == 0 (the invalid sentinel).
ExternalRefResolved resolve_external_ref(
    const ExternalRef& ref,
    ArenaPool&         pool = global_arena_pool()) noexcept;

// Resolve an ExternalRef whose arena_id is MODULE-LOCAL (an index into
// `source_arena`'s import table): translates local → global via the pool, then
// resolves the obj_id normally. ok()=false if the local id can't be resolved.
ExternalRefResolved resolve_external_ref_local(
    arena_id_t         source_arena,
    const ExternalRef& ref,
    ArenaPool&         pool = global_arena_pool()) noexcept;

// Detect + resolve a generic AnyVal slot in one go (transparent cross-arena
// traversal). nullopt if `av` is not an ExternalRef niche or fails to resolve.
inline std::optional<ExternalRefResolved> resolve_if_external(
    AnyVal av, ArenaPool& pool = global_arena_pool()) noexcept
{
    if (!is_external_ref_av(av)) return std::nullopt;
    auto r = resolve_external_ref(decode_external_ref(av), pool);
    if (!r.ok()) return std::nullopt;
    return r;
}

} // namespace logos::hermes2
