// SPDX-License-Identifier: Apache-2.0
// stdlib/rt — process-global block-ops registry.
//
// Mirrors big-Memoria's ProfileMetadata::local() — a singleton table
// keyed by `block_type_hash` (= type_hash::<NodeSchema>(), populated
// at offset 16 of every NodeBase). Each entry holds two type-erased
// fn-pointers:
//
//   for_each_child(node, visit_u64)      — walks the rc-counted children
//                                          of `node` (= branch children
//                                          for branch nodes, leaf vals
//                                          for dir-style leaves, no-op
//                                          for data-style leaves).
//   release(node)                        — releases a single rc on
//                                          `node`; if it was the last,
//                                          calls for_each_child with a
//                                          release visitor and then
//                                          frees the node's resources.
//
// Registration is idempotent on `block_hash`. Lookup returns 0 on miss;
// callers (typically `node_clone_for_cow_c` for retain, `Snap::drop`
// for release) treat 0 as "not registered = no-op".
//
// Concurrency: a single std::mutex guards both registration and lookup.
// Registrations are rare (once per CFG per process at first
// snap_begin / create_pmap); lookups are on the BTree CoW hot path,
// but they fire only at clone/release moments, not per-node-read.
// Should the lock contention ever matter, switch to a striped or
// lock-free map.

#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace {

struct BlockOps {
    uint64_t for_each_child_fp;
    uint64_t release_fp;
};

std::unordered_map<uint64_t, BlockOps>& registry() {
    static std::unordered_map<uint64_t, BlockOps> g_registry;
    return g_registry;
}

std::mutex& registry_mutex() {
    static std::mutex g_mutex;
    return g_mutex;
}

} // namespace

extern "C" {

// Idempotent registration: silently no-op if `block_hash` is already
// present. We do not validate that an existing entry's fps match — that
// would catch certain rebuild-skew bugs but is mostly noise.
void logos_block_ops_register(uint64_t block_hash,
                              uint64_t for_each_child_fp,
                              uint64_t release_fp) {
    std::lock_guard<std::mutex> lock(registry_mutex());
    registry().try_emplace(block_hash, BlockOps{for_each_child_fp, release_fp});
}

uint64_t logos_block_ops_lookup_for_each_child(uint64_t block_hash) {
    std::lock_guard<std::mutex> lock(registry_mutex());
    auto& m = registry();
    auto it = m.find(block_hash);
    return it == m.end() ? 0 : it->second.for_each_child_fp;
}

uint64_t logos_block_ops_lookup_release(uint64_t block_hash) {
    std::lock_guard<std::mutex> lock(registry_mutex());
    auto& m = registry();
    auto it = m.find(block_hash);
    return it == m.end() ? 0 : it->second.release_fp;
}

} // extern "C"
