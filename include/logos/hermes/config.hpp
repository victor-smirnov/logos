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

} // namespace logos::hermes
