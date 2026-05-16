// Logos project — https://github.com/victor-smirnov/logos
//
// ArenaPool: process-global registry mapping arena_id → MemHolder*.
// Phase 0 of the multi-arena IR refactor (see docs/internals/multi-arena-ir.md).
//
// This header lands the API only — no consumers wire through it yet. Phase 1
// (Hermes foundation: ExternalRef, LirArenaRoot, directory) and Phase 3
// (module_loader integration) start using it.
//
// Threading: single-threaded in initial impl (per Q3 in design doc — concurrent
// access is deferred to future Memoria substrate work).

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace logos::hermes {

class MemHolder;

// arena_id_t: 3-byte (24-bit) value packed in u32.
// 0 = invalid sentinel; valid ids start at 1.
// Cap: ~16 M arenas per process — far beyond practical need.
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

// Handle returned by register_module. Carries the assigned arena_id plus a
// snapshot of the registration metadata (name + resolved dep ids). The handle
// itself does NOT own a refcount on the arena — pool retains its own ref.
struct ModuleHandle {
    arena_id_t              arena_id;
    std::string             name;
    std::vector<arena_id_t> depends_on;
};

// ArenaPool: lookup substrate for the multi-arena IR.
//
// Lifetime contract:
//   register_module(mem, ...) increments mem's refcount. Pool releases the
//   ref on unregister(aid) or on pool destruction. Caller may release its own
//   ref independently — the arena lives as long as anyone references it.
//
// arena_id allocation:
//   Append-only per invariant #2 of the design doc. unregister(aid) frees the
//   arena but does NOT recycle the slot — future register_module calls always
//   get a fresh id. This keeps any in-flight ExternalRef from accidentally
//   resolving to a different module after a re-register.
class ArenaPool {
public:
    virtual ~ArenaPool() = default;

    // Register a module's arena.
    // - mem: must be non-null; pool takes a ref via mem->ref().
    // - name: must be unique across currently-registered modules.
    // - dep_names: every entry must already be registered.
    // Returns assigned arena_id (always valid). Asserts on contract violations.
    virtual ModuleHandle register_module(
        MemHolder*               mem,
        std::string              name,
        std::vector<std::string> dep_names) = 0;

    // Release the pool's ref on the arena. After this call, get(aid) returns
    // nullptr and find_by_name(name) returns nullopt for the matching entry.
    // arena_id slot is NOT recycled (per invariant #2).
    virtual void unregister(arena_id_t aid) = 0;

    // Hot path: arena_id → MemHolder*. Returns nullptr for invalid/unregistered.
    // Non-virtual fast path could be added later via CRTP if profiling demands.
    virtual MemHolder* get(arena_id_t aid) noexcept = 0;

    // Cold path: name → handle copy. Returns nullopt if not registered.
    virtual std::optional<ModuleHandle> find_by_name(std::string_view name) = 0;

    // Phase 4.B: name → (arena_id, obj_id) export lookup. Walks every
    // registered arena's LirArenaRoot.EXPORTS map and returns the first
    // hit. Returns INVALID arena_id when not found. Currently linear in
    // (registered arenas) × (1 hash lookup); fine for the expected pool
    // size (≤ ~10 modules per build). Cache hook can be added later.
    struct ExportLookup {
        arena_id_t arena_id;
        uint32_t   obj_id = 0;
        constexpr bool ok() const noexcept { return arena_id.is_valid(); }
    };
    virtual ExportLookup lookup_export(std::string_view name) noexcept = 0;
};

// In-memory implementation. Single-threaded. Slots are append-only;
// arena_ids never recycled.
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

private:
    struct Entry {
        MemHolder*              mem = nullptr;  // null after unregister
        std::string             name;
        std::vector<arena_id_t> deps;
    };
    // Index = arena_id.value. Slot 0 is the invalid sentinel.
    std::vector<Entry> slots_;
    // name → arena_id for live (registered) entries only.
    std::unordered_map<std::string, arena_id_t> by_name_;
};

// Process-global pool accessor. Returns the same InMemoryArenaPool for the
// lifetime of the process. Future swap to MemoriaArenaPool happens here.
ArenaPool& global_arena_pool();

} // namespace logos::hermes
