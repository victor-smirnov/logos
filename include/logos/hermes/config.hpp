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
    out_of_memory  = 0x0001'0003,  // arena allocation failed (OOM)
};

// Strong offset type — kept for SERIALIZATION (a compacted single-segment blob is
// a rigid relocatable block addressed by within-blob offsets). The LIVE form of a
// reference is NOT this: it is a self-relative RelativePtr<T> (see relative_ptr.hpp),
// which needs no base. Default storage: uint32_t (max single-segment blob 4 GB).
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

// Sentinel for null offsets (offset 0 is never a user allocation in a segment).
inline constexpr arena_offset_t NULL_OFFSET{0};

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
