// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <functional>
#include <format> // NOLINT — needed for std::formatter specialization below

namespace logos::hermes {

// ---------------------------------------------------------------------------
// Hermes scalar error codes (range 0x0001'0000 … 0x0001'FFFF).
// ---------------------------------------------------------------------------
enum class ErrCode : uint64_t {
    parse_error    = 0x0001'0001,  // text parser or path parser failure
    template_error = 0x0001'0002,  // template parse / render failure
};

// Strong offset type for arena-relative pointers.
// Prevents accidental implicit conversion from raw integers.
// Default storage: uint32_t (max segment size 4GB).
class arena_offset_t {
public:
    using value_type = uint32_t;

    constexpr arena_offset_t() noexcept : value_(0) {}
    constexpr explicit arena_offset_t(value_type v) noexcept : value_(v) {}

    constexpr value_type value() const noexcept { return value_; }
    constexpr explicit operator value_type() const noexcept { return value_; }

    constexpr auto operator<=>(const arena_offset_t&) const noexcept = default;
    constexpr bool operator==(const arena_offset_t&) const noexcept = default;

private:
    value_type value_;
};

static_assert(sizeof(arena_offset_t) == sizeof(uint32_t));

// Sentinel value for null offsets.
inline constexpr arena_offset_t NULL_OFFSET{~uint32_t(0)};

// DocumentHeader: untagged structure at offset 0 of every arena segment.
struct DocumentHeader {
    arena_offset_t root_offset = NULL_OFFSET;

    bool has_root() const { return root_offset != NULL_OFFSET; }
};

static_assert(sizeof(DocumentHeader) == sizeof(arena_offset_t));

} // namespace logos::hermes

// Allow use as hash key.
template <>
struct std::hash<logos::hermes::arena_offset_t> {
    size_t operator()(logos::hermes::arena_offset_t o) const noexcept {
        return std::hash<uint32_t>{}(o.value());
    }
};

// Allow use with std::format / std::println.
template <>
struct std::formatter<logos::hermes::arena_offset_t> : std::formatter<uint32_t> {
    auto format(logos::hermes::arena_offset_t o, auto& ctx) const {
        return std::formatter<uint32_t>::format(o.value(), ctx);
    }
};
