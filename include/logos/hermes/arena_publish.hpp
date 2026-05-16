// Logos project — https://github.com/victor-smirnov/logos
//
// Arena publish helpers — Phase 1.B of multi-arena IR refactor.
// See docs/internals/multi-arena-ir.md §6.1 for the publish pipeline.
//
// emit_module workflow:
//
//   1. Build the arena (existing sema + mono + borrow flows).
//   2. Build a LirArenaRoot scaffold with empty DIRECTORY array:
//        auto root = lir_arena_root_begin(doc, "stdlib", {"alloc", "coremeta"});
//   3. For every externally-visible object, publish to get an obj_id:
//        auto oid = arena_publish(root, object_anyval);
//   4. Lir_arena_root_finalize attaches root to DocumentHeader + seals:
//        lir_arena_root_finalize(doc, root);
//
// After step 4 the arena is immutable; consumer arenas may safely reference
// {arena_id, oid} via ExternalRef.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <logos/core/expected.hpp>
#include <logos/hermes/view.hpp>
#include <logos/hermes/lir_arena_root.hpp>
#include <logos/hermes/arena_pool.hpp>

namespace logos::hermes {

// In-progress LirArenaRoot accumulator. Holds owning views of the root map
// and its directory array so they stay alive until finalize. Caller appends
// to the directory via arena_publish().
struct ArenaPublishBuilder {
    Hermes  doc;          // owning doc handle
    TinyMap root_map;     // owning root TinyObjectMap (schema = LirArenaRoot)
    Array   directory;    // owning directory ObjectArray
    bool    finalized = false;
};

// Begin a LirArenaRoot in `doc`. Allocates the root TinyObjectMap, the
// directory ObjectArray (with obj_id 0 = null sentinel pre-populated),
// the module name string, and the deps array.
//
// Returns an ArenaPublishBuilder that holds owning handles to the root +
// directory. Caller appends to the directory via arena_publish() and
// terminates with lir_arena_root_finalize().
//
// The caller must keep the Hermes `doc` handle alive for the duration —
// the builder borrows it (does not copy refcount).
[[nodiscard]] logos::expected<ArenaPublishBuilder>
lir_arena_root_begin(Hermes&                       doc,
                     std::string_view              module_name,
                     const std::vector<std::string>& dep_names) noexcept;

// Publish an in-arena object: append to the directory and return its obj_id.
// `target` must be a pointer-mode AnyVal pointing at an object in `b.doc`'s
// arena.
//
// Per invariant #2: obj_ids are append-only — caller never removes or
// reassigns. obj_id 0 is the sentinel "invalid" (pre-populated by begin),
// so real publishes start at 1 (or higher if begin pre-populated more).
[[nodiscard]] logos::expected<uint32_t>
arena_publish(ArenaPublishBuilder& b, AnyVal target) noexcept;

// Append a null entry to the directory — advances the next obj_id slot
// without binding it to an object. Useful for reserving an obj_id ahead
// of constructing the target (or for matching a pre-declared obj_id
// expected by a consumer).
[[nodiscard]] logos::expected<uint32_t>
arena_publish_reserved(ArenaPublishBuilder& b) noexcept;

// Attach the LirArenaRoot to DocumentHeader.root_offset and seal the
// arena. After this call, the builder is consumed (finalized = true) and
// the arena is read-only. Returns the offset of the LirArenaRoot for
// callers that want to record it (otherwise discarded).
[[nodiscard]] logos::expected<arena_offset_t>
lir_arena_root_finalize(ArenaPublishBuilder& b) noexcept;

// Inspect `doc` for a LirArenaRoot at DocumentHeader.root_offset.
// If present (schema_type_code == type_hash::LirArenaRoot), extracts
// MODULE_NAME + DEPS and calls global_arena_pool().register_module(...).
// Returns the assigned ModuleHandle on success.
//
// Errors (returned as logos::expected error):
//   - doc has no root
//   - root is not a TinyObjectMap with LirArenaRoot schema
//   - MODULE_NAME field missing or malformed
//   - any dep in DEPS isn't yet registered (per invariant #9 — DAG order)
//
// emit_module's eager-mode pipeline calls this immediately after
// lir_arena_root_finalize. module_loader calls this after from_bytes_copy
// when reading a .hermes0 LIR blob.
//
// `pool` parameter defaults to global_arena_pool(); tests/dev tools may
// pass a private InMemoryArenaPool for isolation.
[[nodiscard]] logos::expected<ModuleHandle>
register_lir_arena(Hermes& doc, ArenaPool& pool = global_arena_pool()) noexcept;

} // namespace logos::hermes
