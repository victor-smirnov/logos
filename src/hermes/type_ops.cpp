// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/type_ops.hpp>

#include <cassert>
#include <cstring>

namespace logos::hermes {

// ---------------------------------------------------------------------------
// Linker-section symbols synthesised by the GNU/LLVM linker for any section
// whose name is a valid C identifier.  The section contains TypeOps structs
// placed there by HERMES_REGISTER_TYPE().
//
// Sentinel — ensures the hermes_typeops section always exists in this TU so
// that __start_hermes_typeops/__stop_hermes_typeops are always defined by the
// linker, even when no other object contributing entries is pulled in.
// Entries with type_code == 0 are skipped in hermes_init().
// ---------------------------------------------------------------------------
[[gnu::section("hermes_typeops"), gnu::used]]
static const TypeOps hermes_typeops_sentinel = {};  // type_code == 0 → skip

extern "C" {
    extern const TypeOps __start_hermes_typeops[];
    extern const TypeOps __stop_hermes_typeops[];
}

// ---------------------------------------------------------------------------
// Core table — direct indexed by type_code for type_code < 128.
// ---------------------------------------------------------------------------
static const TypeOps* g_core_ops[128] = {};

// ---------------------------------------------------------------------------
// ExtTypeTable — cuckoo hash table for extension types (type_code >= 128).
//
// Two sub-tables t1 / t2, each of size N (power of two).
// Lookup: at most 2 array accesses + 2 comparisons — no chains, no search.
// Insert: runs at startup only; eviction loop terminates for load < ~80%.
// ---------------------------------------------------------------------------
struct ExtTypeTable {
    static constexpr uint32_t N = 256;

    const TypeOps* t1[N] = {};
    const TypeOps* t2[N] = {};

    const TypeOps* find(uint64_t tc) const noexcept {
        const TypeOps* a = t1[tc & (N - 1)];
        if (a && a->type_code == tc) return a;
        const TypeOps* b = t2[(tc >> 32) & (N - 1)];
        if (b && b->type_code == tc) return b;
        return nullptr;
    }

    // Returns false only on a cycle (means N is too small — should not happen
    // in practice since N >= 4 * expected_count).
    bool insert(const TypeOps* ops) noexcept {
        uint64_t tc = ops->type_code;
        for (int kick = 0; kick < 64; ++kick) {
            const TypeOps*& s1 = t1[tc & (N - 1)];
            if (!s1) { s1 = ops; return true; }
            const TypeOps* evicted = s1;
            s1 = ops;
            ops = evicted;
            tc  = ops->type_code;

            const TypeOps*& s2 = t2[(tc >> 32) & (N - 1)];
            if (!s2) { s2 = ops; return true; }
            evicted = s2;
            s2  = ops;
            ops = evicted;
            tc  = ops->type_code;
        }
        return false; // cycle — increase N if this ever triggers
    }
};

static ExtTypeTable g_ext_table;
static bool         g_initialized = false;

// ---------------------------------------------------------------------------
// hermes_init()
// ---------------------------------------------------------------------------
// Link-time anchors — force archive members carrying HERMES_REGISTER_TYPE
// entries to be pulled in even when nothing else references them.
void hermes_map_i32_anyval_anchor() noexcept;
void hermes_stringify_anchor()      noexcept;
void hermes_typed_array_anchor()    noexcept;

void hermes_init() noexcept {
    if (g_initialized) return;
    g_initialized = true;

    // Touch anchors so their translation units are linked in.
    hermes_map_i32_anyval_anchor();
    hermes_stringify_anchor();
    hermes_typed_array_anchor();

    for (const TypeOps* p = __start_hermes_typeops;
         p != __stop_hermes_typeops; ++p)
    {
        if (!p->type_code) continue;  // skip sentinel (type_code == 0)
        if (p->type_code < 128) {
            g_core_ops[p->type_code] = p;
        } else {
            bool ok = g_ext_table.insert(p);
            assert(ok && "hermes_typeops cuckoo table full — increase ExtTypeTable::N");
            (void)ok;
        }
    }
}

// ---------------------------------------------------------------------------
// find_type_ops()
// ---------------------------------------------------------------------------
const TypeOps* find_type_ops(uint64_t type_code) noexcept {
    if (type_code < 128)
        return g_core_ops[type_code];
    return g_ext_table.find(type_code);
}

} // namespace logos::hermes
