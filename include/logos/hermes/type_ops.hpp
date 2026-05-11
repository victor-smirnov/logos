// Logos project — https://github.com/victor-smirnov/logos
//
// type_ops.hpp — runtime type dispatch for Hermes.
//
// Two-tier lookup:
//   Core types  (type_code < 128) : direct array index — one load, no search.
//   Extension types               : cuckoo hash table  — at most 2 probes.
//
// Libraries register their own types by placing a TypeOps struct in the
// "hermes_typeops" linker section via HERMES_REGISTER_TYPE().
// hermes_init() collects all entries and builds both tables once at startup.

#pragma once

#include <cstdint>
#include <string>
#include <logos/core/expected.hpp>
#include <logos/hermes/any_val.hpp>

namespace logos::hermes {

// Forward declaration — defined in clone.hpp.
struct CloneCtx;

// ---------------------------------------------------------------------------
// StringifyCtx — context threaded through all stringify dispatch functions.
// Extension types use recurse_anyval / recurse_tagged to stringify children.
// ---------------------------------------------------------------------------
struct StringifyCtx {
    uint8_t*     base;    // arena base (non-const: matches existing API convention)
    bool         pretty;
    int          indent;
    std::string* out;

    // Stringify a child held in an AnyVal slot (value-mode or pointer-mode).
    logos::expected<void> (*recurse_anyval)(const AnyVal* slot, StringifyCtx* ctx) noexcept;

    // Stringify a tagged object; obj points at the first data byte
    // (TypeTag is stored *before* obj in the arena).
    logos::expected<void> (*recurse_tagged)(const uint8_t* obj, StringifyCtx* ctx) noexcept;
};

// ---------------------------------------------------------------------------
// TypeOps — per-type operation table.
// ---------------------------------------------------------------------------
struct TypeOps {
    uint64_t type_code;

    // Stringify a tagged (pointer-mode) value.  Required — must not be null.
    logos::expected<void> (*stringify_tagged)(const uint8_t* obj, StringifyCtx* ctx) noexcept;

    // Stringify a value-mode AnyVal.  Null if this type is never embeddable.
    logos::expected<void> (*stringify_embed)(const AnyVal* slot, StringifyCtx* ctx) noexcept;

    // Compare two values in the arena.  Returns <0 / 0 / >0.
    // Null if comparison is not supported for this type.
    int (*compare)(const uint8_t* a, const uint8_t* b, uint8_t* base) noexcept;

    // Clone a tagged object into ctx->dst arena; returns dst-arena offset.
    // Null if this type doesn't support cloning.
    logos::expected<uint32_t> (*clone_tagged)(const uint8_t* obj, CloneCtx* ctx) noexcept;
};

// ---------------------------------------------------------------------------
// Dispatch API
// ---------------------------------------------------------------------------

// Collect all TypeOps from the linker section and build the dispatch tables.
// Must be called once before any stringify / compare operation.
void hermes_init() noexcept;

// Return the TypeOps for a type_code, or nullptr if unknown.
// O(1) for core types; ≤2 probes for extension types.
const TypeOps* find_type_ops(uint64_t type_code) noexcept;

// ---------------------------------------------------------------------------
// HERMES_REGISTER_TYPE — self-registration macro.
//
// Place once in a library .cpp alongside the TypeOps definition.
// The linker collects all marked entries; hermes_init() picks them up
// automatically — no central list, no registration call in main().
//
// Usage:
//   static const TypeOps k_my_ops = { MY_TYPE_CODE, my_stringify, ... };
//   HERMES_REGISTER_TYPE(k_my_ops)
// ---------------------------------------------------------------------------
#define HERMES_CONCAT_(a, b) a##b
#define HERMES_CONCAT(a, b)  HERMES_CONCAT_(a, b)

#define HERMES_REGISTER_TYPE(ops_var)                                     \
    [[gnu::section("hermes_typeops"), gnu::used]]                         \
    static const ::logos::hermes::TypeOps                                 \
        HERMES_CONCAT(hermes_typeops_entry_, __LINE__) = (ops_var)

} // namespace logos::hermes
