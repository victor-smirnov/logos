// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Minimal header-only SHA-256 implementation.
// No external dependencies. Used for computing type hashes.

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace logos::compiler {

namespace sha256_detail {

static constexpr uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

inline uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

inline void process_block(const uint8_t* block, uint32_t h[8]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (uint32_t(block[i*4+0]) << 24) | (uint32_t(block[i*4+1]) << 16)
             | (uint32_t(block[i*4+2]) <<  8) | (uint32_t(block[i*4+3]));
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr(w[i-2],  17) ^ rotr(w[i-2],  19) ^ (w[i-2]  >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], hh=h[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1    = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch    = (e & f) ^ (~e & g);
        uint32_t temp1 = hh + S1 + ch + K[i] + w[i];
        uint32_t S0    = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj   = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        hh = g; g = f; f = e; e = d + temp1;
        d  = c; c = b; b = a; a = temp1 + temp2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
    h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
}

} // namespace sha256_detail

// Compute SHA-256 of the input string. Returns 32 bytes (256 bits).
inline std::array<uint8_t, 32> sha256(std::string_view input) {
    using namespace sha256_detail;

    uint32_t h[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };

    const uint8_t* data = reinterpret_cast<const uint8_t*>(input.data());
    uint64_t len = input.size();
    uint64_t bit_len = len * 8;

    // Process full 64-byte blocks.
    uint64_t full_blocks = len / 64;
    for (uint64_t i = 0; i < full_blocks; ++i)
        process_block(data + i * 64, h);

    // Final block(s): padding.
    uint8_t last[128] = {};
    uint64_t tail = len % 64;
    std::memcpy(last, data + full_blocks * 64, tail);
    last[tail] = 0x80;

    // If no room for 8-byte length at end of this block, use two blocks.
    int nblocks = (tail < 56) ? 1 : 2;
    // Write big-endian bit length in last 8 bytes of final block.
    uint8_t* lenpos = last + (nblocks * 64 - 8);
    for (int i = 7; i >= 0; --i) { lenpos[i] = uint8_t(bit_len & 0xFF); bit_len >>= 8; }

    for (int i = 0; i < nblocks; ++i)
        process_block(last + i * 64, h);

    // Serialize big-endian.
    std::array<uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i) {
        out[i*4+0] = uint8_t(h[i] >> 24);
        out[i*4+1] = uint8_t(h[i] >> 16);
        out[i*4+2] = uint8_t(h[i] >>  8);
        out[i*4+3] = uint8_t(h[i]);
    }
    return out;
}

// Compute type hash: SHA-256 truncated to 23 bytes.
inline std::array<uint8_t, 23> type_hash_23(std::string_view canonical_name) {
    auto full = sha256(canonical_name);
    std::array<uint8_t, 23> out{};
    std::memcpy(out.data(), full.data(), 23);
    return out;
}

// Extract first 7 bytes (56 bits) of type hash as uint64_t (big-endian, zero-padded).
// Used as the auto-assigned type_code for zone datatypes (codes >= 128).
inline uint64_t type_hash_56bit(const std::array<uint8_t, 23>& hash) {
    uint64_t v = 0;
    for (int i = 0; i < 7; ++i)
        v = (v << 8) | hash[i];
    return v;
}

// Extract first 8 bytes (64 bits) of type hash as uint64_t (big-endian).
// Used as TypeUID — the runtime-visible identity carried by the
// metaprog `Type` value-handle. Reverse lookup (uid → TypeRef) is
// per-Program; antiquot reification reads this and recovers the
// source TypeRef. Collision risk is 2^-64; link-time check parallels
// the existing tag-dispatch design.
inline uint64_t type_hash_64bit(const std::array<uint8_t, 23>& hash) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | hash[i];
    return v;
}

} // namespace logos::compiler
