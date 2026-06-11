// Logos project — https://github.com/victor-smirnov/logos
//
// Arena publish helpers (Hermes) — build a LirArenaRoot in a document, publish
// externally-visible objects to its DIRECTORY (obj_id = index) + EXPORTS (name →
// obj_id), then finalize (set as doc root + seal). After finalize the arena is
// immutable; consumer arenas reference {arena_id, obj_id} via an ExternalRef Pod.
//
// Flow (emit_module):
//   1. Build the LIR objects in a HermesCtr (sema + mono + borrow).
//   2. auto b = lir_arena_root_begin(doc, "stdlib", {"alloc", "coremeta"});
//   3. auto oid = arena_publish_named(b, mangled_name, object_av);   // per export
//   4. lir_arena_root_finalize(b);                                    // seal
//   5. compactify(doc) → single-segment blob → dump.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <logos/core/expected.hpp>
#include <logos/hermes/document.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/object_map.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/arena_pool.hpp>

namespace logos::hermes {

// In-progress LirArenaRoot accumulator. Holds the borrowed doc + RAW pointers to the
// root map / directory / exports — stable because the never-move (MultiChunk) arena
// keeps object headers fixed across further allocations.
struct ArenaPublishBuilder {
    HermesCtr*     doc       = nullptr;   // borrowed (caller keeps it alive)
    TinyObjectMap* root_map  = nullptr;   // schema_type_code == LirArenaRoot
    ObjectArray*   directory = nullptr;
    ObjectMap*     exports   = nullptr;
    bool           finalized = false;

    Arena& arena() const noexcept { return doc->arena(); }
};

// Begin a LirArenaRoot in `doc`. Allocates the root TinyObjectMap (schema tagged),
// directory (obj_id 0 = null sentinel pre-populated), module-name string, deps
// array, and exports map; wires them into the root's fields.
[[nodiscard]] logos::expected<ArenaPublishBuilder>
lir_arena_root_begin(HermesCtr&                       doc,
                     std::string_view                module_name,
                     const std::vector<std::string>& dep_names) noexcept;

// Publish an in-arena object (a value-form Ref AnyVal pointing into `doc`'s arena):
// append to the directory, return its obj_id. Append-only; obj_id 0 is the sentinel.
[[nodiscard]] logos::expected<uint32_t>
arena_publish(ArenaPublishBuilder& b, AnyVal target) noexcept;

// Append a null directory entry, reserving the next obj_id without binding it.
[[nodiscard]] logos::expected<uint32_t>
arena_publish_reserved(ArenaPublishBuilder& b) noexcept;

// Publish AND register in EXPORTS under `name` (obj_id encoded as a u24 Pod). Caller
// owns name uniqueness (a later put overwrites).
[[nodiscard]] logos::expected<uint32_t>
arena_publish_named(ArenaPublishBuilder& b,
                    std::string_view     name,
                    AnyVal               target) noexcept;

// Attach the LirArenaRoot to DocumentHeader.root and seal. Consumes the builder
// (finalized = true). Returns a value-form Ref to the root.
[[nodiscard]] logos::expected<AnyVal>
lir_arena_root_finalize(ArenaPublishBuilder& b) noexcept;

// Inspect `doc` for a LirArenaRoot at its root and register it with the pool
// (extracts MODULE_NAME + DEPS, calls register_module). Idempotent on name.
[[nodiscard]] logos::expected<ModuleHandle>
register_lir_arena(HermesCtr& doc, ArenaPool& pool = global_arena_pool()) noexcept;

} // namespace logos::hermes
