// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>

namespace logos::hermes {

// FNV-1a 64-bit hash. Used for string hashing in ObjectMap.
// Reference: http://www.isthe.com/chongo/tech/comp/fnv/

inline constexpr uint64_t fnv1a_offset_basis = 0xCBF29CE484222325ULL;
inline constexpr uint64_t fnv1a_prime        = 0x00000100000001B3ULL;

inline uint64_t fnv1a_hash(const uint8_t* data, size_t length) noexcept {
    uint64_t h = fnv1a_offset_basis;
    for (size_t i = 0; i < length; ++i) {
        h ^= data[i];
        h *= fnv1a_prime;
    }
    return h;
}

inline uint64_t fnv1a_hash(std::string_view sv) noexcept {
    return fnv1a_hash(reinterpret_cast<const uint8_t*>(sv.data()), sv.size());
}

} // namespace logos::hermes
