// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <cstddef>
#include <utility>

namespace logos::hermes {

// Variable-length unsigned integer encoding (up to 56 bits).
//
// Encoding scheme:
//   value < 249        → 1 byte:  [value]
//   value >= 249       → 1 + N bytes: [248 + N] [value in N bytes, little-endian]
//                        where N = minimum bytes to represent the value (1..7)
//
// Maximum encodable value: 0x00FFFFFFFFFFFFFF (56 bits).

struct VarIntResult {
    uint64_t value;
    size_t bytes_read;
};

// Encode value into buf. Returns the number of bytes written (1 to 8).
// Caller must ensure buf has at least 8 bytes of space.
inline size_t varint_encode(uint64_t value, uint8_t* buf) {
    if (value < 249) {
        buf[0] = static_cast<uint8_t>(value);
        return 1;
    }

    // Count bytes needed for the value.
    uint8_t n = 1;
    uint64_t v = value >> 8;
    while (v != 0) {
        ++n;
        v >>= 8;
    }

    buf[0] = static_cast<uint8_t>(248 + n);
    for (uint8_t i = 0; i < n; ++i) {
        buf[1 + i] = static_cast<uint8_t>(value >> (i * 8));
    }
    return 1 + n;
}

// Decode a varint from buf. Returns the value and number of bytes consumed.
inline VarIntResult varint_decode(const uint8_t* buf) {
    uint8_t first = buf[0];
    if (first < 249) {
        return {first, 1};
    }

    uint8_t n = first - 248;
    uint64_t value = 0;
    for (uint8_t i = 0; i < n; ++i) {
        value |= static_cast<uint64_t>(buf[1 + i]) << (i * 8);
    }
    return {value, static_cast<size_t>(1 + n)};
}

} // namespace logos::hermes
