// Logos project — https://github.com/victor-smirnov/logos
//
// clone.hpp — deep clone of a Hermes document into a fresh packed arena.
//
// Port of `document_compactify` from stdlib/hermes/clone.logos. Dispatches via
// TypeOps::clone_tagged. A DAG cycle cache (src_off → dst_off) breaks shared
// subgraphs into a single clone per source object.
//
// Step 1: plain clone, no PARAM slot tracking. The `out_params` vector is
// accepted but not populated — PARAM tracking will be wired in a follow-up.

#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>

#include <logos/hermes/document.hpp>
#include <logos/hermes/arena.hpp>
#include <logos/core/expected.hpp>

namespace logos::hermes {

// One PARAM (type_code 127) slot recorded during a clone walk.
//   offset      — dst-arena byte offset of the AnyVal slot holding the PARAM.
//   value_index — u24 payload of the PARAM AnyVal (template capture index).
struct ParamSlot {
    uint32_t offset;
    uint32_t value_index;
};

// Context threaded through the clone walk.
struct CloneCtx {
    const uint8_t* base_src;
    Arena*         dst;
    // src_off → dst_off. Each per-type clone_tagged handler inserts its own
    // entry BEFORE recursing into children so back-edges in a cyclic graph
    // resolve to the already-allocated dst object.
    std::unordered_map<uint32_t, uint32_t> map;
    // If non-null, PARAM-slot offsets are appended here. Unused in step 1.
    std::vector<ParamSlot>* out_params;
};

// Clone `src` into a fresh packed Hermes. If `out_params` is non-null, PARAM
// slots encountered during the walk are appended to it (step 2).
[[nodiscard]] logos::expected<Hermes>
clone(const HermesView& src,
      std::vector<ParamSlot>* out_params = nullptr) noexcept;

// Clone an AnyVal value (inline or pointer) into ctx->dst. For pointer mode:
// looks up cycle cache first; on miss, dispatches via clone_tagged. Returns
// the new AnyVal raw (u32).
[[nodiscard]] logos::expected<uint32_t>
anyval_clone(uint32_t src_raw, CloneCtx* ctx) noexcept;

} // namespace logos::hermes
