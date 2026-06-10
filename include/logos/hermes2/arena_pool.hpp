// Logos project — https://github.com/victor-smirnov/logos
//
// ArenaPool (Hermes2 port) — process-global registry mapping arena_id → MemHolder.
// The lookup substrate for the multi-arena IR. Ported from src/hermes/arena_pool.*
// onto the hermes2 MemHolder; the API + invariants are unchanged so logosc's
// call-sites move with a `hermes::` → `hermes2::` rename.
//
// Threading: single-threaded (concurrent access is future Memoria-substrate work).

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <logos/hermes2/import_table.hpp>  // ImportEntry

namespace logos::hermes2 {

class MemHolder;

// arena_id_t: 3-byte (24-bit) value packed in u32. 0 = invalid sentinel; valid ids
// start at 1. Cap ~16M arenas per process.
struct arena_id_t {
    uint32_t value = 0;

    constexpr arena_id_t() = default;
    constexpr explicit arena_id_t(uint32_t v) : value(v) {}

    constexpr bool is_valid() const noexcept { return value != 0; }
    constexpr bool operator==(const arena_id_t&) const noexcept = default;
    constexpr bool operator!=(const arena_id_t&) const noexcept = default;
};

inline constexpr arena_id_t INVALID_ARENA_ID{};
inline constexpr uint32_t   MAX_ARENA_ID_VALUE = 0x00FFFFFF;  // 24-bit max

// Handle returned by register_module: the assigned arena_id + registration
// metadata. Does NOT own a refcount on the arena — the pool retains its own ref.
struct ModuleHandle {
    arena_id_t              arena_id;
    std::string             name;
    std::vector<arena_id_t> depends_on;
};

// ArenaPool — lookup substrate for the multi-arena IR.
//
// Lifetime: register_module(mem,...) takes a +1 ref on mem (via mem->ref()); the
// pool releases it on unregister(aid) or pool destruction. The caller's own ref is
// independent — the arena lives as long as anyone references it.
//
// arena_id allocation is append-only (invariant #2): unregister frees the arena but
// never recycles the slot, so an in-flight ExternalRef can never resolve to a
// different module after a re-register.
class ArenaPool {
public:
    virtual ~ArenaPool() = default;

    virtual ModuleHandle register_module(
        MemHolder*               mem,
        std::string              name,
        std::vector<std::string> dep_names) = 0;

    virtual void unregister(arena_id_t aid) = 0;

    // Hot path: arena_id → MemHolder*. nullptr for invalid/unregistered.
    virtual MemHolder* get(arena_id_t aid) noexcept = 0;

    // Cold path: name → handle copy. nullopt if not registered.
    virtual std::optional<ModuleHandle> find_by_name(std::string_view name) = 0;

    // name → (arena_id, obj_id) export lookup. Walks every registered arena's
    // LirArenaRoot EXPORTS map; first hit wins. INVALID arena_id when absent.
    struct ExportLookup {
        arena_id_t arena_id;
        uint32_t   obj_id = 0;
        constexpr bool ok() const noexcept { return arena_id.is_valid(); }
    };
    virtual ExportLookup lookup_export(std::string_view name) noexcept = 0;

    // ── Module-local arena_id resolution (import tables) ──────────────────────
    // A cross-arena ExternalRef's arena_id is MODULE-LOCAL — an index into the
    // referencing module's import table. The loader attaches each module's import
    // entries via set_module_imports; resolve_local_arena_id translates a
    // (source module, local arena_id) pair to the global arena_id.

    virtual void set_module_imports(arena_id_t                aid,
                                    std::string               file_name,
                                    std::vector<ImportEntry>  imports) = 0;

    virtual arena_id_t find_arena_by_file(std::string_view file_name) noexcept = 0;

    virtual arena_id_t resolve_local_arena_id(arena_id_t source_aid,
                                              arena_id_t local_aid) noexcept = 0;
};

// In-memory implementation. Single-threaded; slots append-only.
class InMemoryArenaPool final : public ArenaPool {
public:
    InMemoryArenaPool();
    ~InMemoryArenaPool() override;

    InMemoryArenaPool(const InMemoryArenaPool&)            = delete;
    InMemoryArenaPool& operator=(const InMemoryArenaPool&) = delete;

    ModuleHandle register_module(MemHolder*               mem,
                                  std::string              name,
                                  std::vector<std::string> dep_names) override;
    void unregister(arena_id_t aid) override;
    MemHolder* get(arena_id_t aid) noexcept override;
    std::optional<ModuleHandle> find_by_name(std::string_view name) override;
    ExportLookup lookup_export(std::string_view name) noexcept override;
    void set_module_imports(arena_id_t                aid,
                            std::string               file_name,
                            std::vector<ImportEntry>  imports) override;
    arena_id_t find_arena_by_file(std::string_view file_name) noexcept override;
    arena_id_t resolve_local_arena_id(arena_id_t source_aid,
                                      arena_id_t local_aid) noexcept override;

private:
    struct Entry {
        MemHolder*              mem = nullptr;  // null after unregister
        std::string             name;
        std::vector<arena_id_t> deps;
        std::string               file_name;   // this module's archive basename
        std::vector<ImportEntry>  imports;      // indexed by local arena_id
        std::vector<arena_id_t>   import_map;    // lazy local→global cache
    };
    std::vector<Entry> slots_;                                  // index = arena_id.value
    std::unordered_map<std::string, arena_id_t> by_name_;
    std::unordered_map<std::string, arena_id_t> by_file_;
};

// Process-global pool. Same InMemoryArenaPool for the process lifetime.
ArenaPool& global_arena_pool();

} // namespace logos::hermes2
