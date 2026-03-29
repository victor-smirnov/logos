// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>

namespace logos::hermes {

// Offset type for all arena-relative pointers.
// Default: uint32_t (max segment size 4GB).
// Override to uint64_t for larger segments.
#ifndef LOGOS_HERMES_OFFSET_TYPE
using arena_offset_t = uint32_t;
#else
using arena_offset_t = LOGOS_HERMES_OFFSET_TYPE;
#endif

// Sentinel value for null offsets.
inline constexpr arena_offset_t NULL_OFFSET = ~arena_offset_t(0);

// DocumentHeader: untagged structure at offset 0 of every arena segment.
struct DocumentHeader {
    // Forward-declared RelativePtr<void> — just stores an arena_offset_t.
    arena_offset_t root_offset = NULL_OFFSET;

    bool has_root() const { return root_offset != NULL_OFFSET; }
};

static_assert(sizeof(DocumentHeader) == sizeof(arena_offset_t));

} // namespace logos::hermes
