// Logos project — https://github.com/victor-smirnov/logos
//
// LirArenaRoot: per-arena metadata anchor for multi-arena IR.
// Phase 1.A of the multi-arena IR refactor.
//
// LirArenaRoot is a TinyObjectMap whose `schema_type_code_` is set to
// `type_hash::LirArenaRoot` (5002). The map is allocated as a normal
// TinyObjectMap (structural TypeTag = TinyObjectMap = 98); the schema
// discriminator distinguishes it from generic maps.
//
// `DocumentHeader.root_offset` points to the LirArenaRoot for arenas that
// participate in the multi-arena IR. AST arenas (pre-existing) keep their
// PROGRAM-node root — the loader knows which to expect based on context
// (LIR mirror blob section vs AST file).
//
// Schema (byte-keyed fields per TinyObjectMap convention):
//   SCHEMA_VERSION (key 0) : uint32_t embedded AnyVal
//   MODULE_NAME    (key 1) : AnyVal pointer → ArenaString
//   DEPS           (key 2) : AnyVal pointer → ObjectArray<AnyVal>
//                            (each entry is an AnyVal pointer → ArenaString)
//   DIRECTORY      (key 3) : AnyVal pointer → ObjectArray<AnyVal>
//                            (each entry is an AnyVal pointer → published
//                             object; nulls = sparse / deprecated slots)

#pragma once

#include <cstdint>
#include <logos/hermes/named_code.hpp>
#include <logos/hermes/view.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/type_registry.hpp>

namespace logos::hermes::lir_arena_root {

// Byte-keyed schema fields. Compact (single-byte keys per TinyObjectMap).
inline constexpr ::logos::hermes::NamedCode<uint8_t> SCHEMA_VERSION{"schema_version", 0};
inline constexpr ::logos::hermes::NamedCode<uint8_t> MODULE_NAME   {"module_name",    1};
inline constexpr ::logos::hermes::NamedCode<uint8_t> DEPS          {"deps",           2};
inline constexpr ::logos::hermes::NamedCode<uint8_t> DIRECTORY     {"directory",      3};

// Current writer always emits this version. Readers tolerate versions <=
// CURRENT_VERSION; unknown future versions trigger an explicit fallback
// (loader treats arena as opaque).
inline constexpr uint32_t CURRENT_VERSION = 1;

} // namespace logos::hermes::lir_arena_root

namespace logos::hermes {

// Lightweight view over a TinyObjectMap known to be a LirArenaRoot.
// Caller is responsible for verifying `schema_type_code() == LirArenaRoot`
// before constructing this view.
class LirArenaRootView {
public:
    explicit LirArenaRootView(TinyMapView map) noexcept : map_(map) {}

    // Schema version. Returns 0 if SCHEMA_VERSION key absent or malformed.
    uint32_t schema_version() const noexcept {
        AnyVal v = map_.get(lir_arena_root::SCHEMA_VERSION.code);
        if (v.is_null() || !v.is_value()) return 0;
        return v.as_value<uint32_t>();
    }

    // Module name. Returns null StringView if MODULE_NAME absent.
    StringView module_name() const noexcept {
        AnyVal v = map_.get(lir_arena_root::MODULE_NAME.code);
        if (v.is_null() || !v.is_pointer()) return StringView{};
        return StringView(v.to_offset(), map_.holder());
    }

    // Dependency list (array of module-name strings). Returns null ArrayView
    // if DEPS absent.
    ArrayView deps() const noexcept {
        AnyVal v = map_.get(lir_arena_root::DEPS.code);
        if (v.is_null() || !v.is_pointer()) return ArrayView{};
        return ArrayView(v.to_offset(), map_.holder());
    }

    // Object directory (array indexed by obj_id; entries point at published
    // objects). Returns null ArrayView if DIRECTORY absent.
    ArrayView directory() const noexcept {
        AnyVal v = map_.get(lir_arena_root::DIRECTORY.code);
        if (v.is_null() || !v.is_pointer()) return ArrayView{};
        return ArrayView(v.to_offset(), map_.holder());
    }

    // Underlying map access for advanced consumers.
    const TinyMapView& map() const noexcept { return map_; }

private:
    TinyMapView map_;
};

} // namespace logos::hermes
