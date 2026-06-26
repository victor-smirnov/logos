// Logos project — https://github.com/victor-smirnov/logos
//
// LirArenaRoot (Hermes) — per-arena metadata anchor for the multi-arena IR.
//
// A TinyObjectMap whose schema_type_code == lir_arena_root::SCHEMA_CODE (the
// first-class TOM discriminator, byte-shared with the Logos HMap<Hu6,HAny>). The
// DocumentHeader.root points at it for arenas that participate in the multi-arena IR.
//
// Schema (byte-keyed fields):
//   SCHEMA_VERSION (key 0) : u24-embedded AnyVal Pod
//   MODULE_NAME    (key 1) : Ref → ArenaString
//   DEPS           (key 2) : Ref → ObjectArray (each entry Ref → ArenaString)
//   DIRECTORY      (key 3) : Ref → ObjectArray (obj_id → published object; nulls = sparse)
//   EXPORTS        (key 4) : Ref → ObjectMap (mangled name → u24 obj_id Pod)

#pragma once

#include <cstdint>

#include <logos/writ/view.hpp>

namespace logos::writ::lir_arena_root {

inline constexpr uint8_t SCHEMA_VERSION = 0;
inline constexpr uint8_t MODULE_NAME    = 1;
inline constexpr uint8_t DEPS           = 2;
inline constexpr uint8_t DIRECTORY      = 3;
inline constexpr uint8_t EXPORTS        = 4;

inline constexpr uint64_t SCHEMA_CODE     = 5002;   // TinyObjectMap.schema_type_code tag
inline constexpr uint32_t CURRENT_VERSION = 1;

} // namespace logos::writ::lir_arena_root

namespace logos::writ {

// Lightweight view over a TinyObjectMap known to be a LirArenaRoot. Caller verifies
// map.ptr()->schema_type_code() == lir_arena_root::SCHEMA_CODE first.
class LirArenaRootView {
public:
    explicit LirArenaRootView(TinyMapView map) noexcept : map_(map) {}

    uint32_t schema_version() const noexcept {
        AnyVal v = map_.get(lir_arena_root::SCHEMA_VERSION);
        return v.is_pod() ? static_cast<uint32_t>(v.as_i56()) : 0;
    }

    StringView module_name() const noexcept {
        return as_string(map_.get(lir_arena_root::MODULE_NAME), map_.holder());
    }
    ArrayView deps() const noexcept {
        return as_array(map_.get(lir_arena_root::DEPS), map_.holder());
    }
    ArrayView directory() const noexcept {
        return as_array(map_.get(lir_arena_root::DIRECTORY), map_.holder());
    }
    MapView exports() const noexcept {
        return as_map(map_.get(lir_arena_root::EXPORTS), map_.holder());
    }

    const TinyMapView& map() const noexcept { return map_; }

private:
    TinyMapView map_;
};

} // namespace logos::writ
