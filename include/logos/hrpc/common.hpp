// Logos project — https://github.com/victor-smirnov/logos
//
// HRPC common types — wire-format compatible with Memoria HRPC.
//
// MessageHeader layout (16 bytes fixed, little-endian):
//   bytes [0..3]  = message_size  (total message size including header)
//   bytes [4..7]  = bits          ([5:0]=message_type, [23:8]=channel_code, [27:24]=optionals)
//   bytes [8..15] = call_id
//
// Optional fields follow the fixed header:
//   optionals bit 1 (value 2) = endpoint_id (32 bytes)

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>

#include <logos/core/expected.hpp>

namespace logos::hrpc {

// ---------------------------------------------------------------------------
// HRPC error codes (range 0x0002'0000 … 0x0002'FFFF).
// ---------------------------------------------------------------------------
enum class ErrCode : uint64_t {
    transport_error = 0x0002'0001,  // TCP connect / read / write failure
    out_of_memory   = 0x0002'0002,  // arena or container allocation failed
};

// ---------------------------------------------------------------------------
// Fundamental types
// ---------------------------------------------------------------------------

// 256-bit endpoint identifier.
using EndpointID  = std::array<uint8_t, 32>;
using CallID      = uint64_t;
using ChannelCode = uint16_t;

enum class SessionSide : uint8_t {
    Client = 0,
    Server = 1,
};

// Message type codes — numeric values match Memoria HRPC for wire compatibility.
enum class MessageType : uint8_t {
    SessionStart          =  0,
    SessionClose          =  1,
    Call                  =  2,
    Return                =  3,
    CallChannelMessage    =  4,
    ContextChannelMessage =  5,
    CallCloseInput        =  6,
    ContextCloseInput     =  7,
    CallCloseOutput       =  8,
    ContextCloseOutput    =  9,
    CallBufferReset       = 10,
    ContextBufferReset    = 11,
    CancelCall            = 12,
};

// ---------------------------------------------------------------------------
// EndpointIDHash — for use with std::unordered_map<EndpointID, ...>
//
// Strategy: reinterpret the first 8 bytes as a uint64_t. Fast, good enough
// for unique 256-bit random IDs (no collision pressure).
// ---------------------------------------------------------------------------
struct EndpointIDHash {
    size_t operator()(const EndpointID& id) const noexcept {
        uint64_t h = 0;
        std::memcpy(&h, id.data(), sizeof(h));
        return static_cast<size_t>(h);
    }
};

// ---------------------------------------------------------------------------
// MessageHeader — 16 bytes, alignas(8).
//
// Bit layout of 'bits' field (uint32_t):
//   [5:0]   message_type   (6 bits)
//   [7:6]   reserved
//   [23:8]  channel_code   (16 bits)
//   [27:24] optionals      (4 bits; bit 1 = has_endpoint_id)
//   [31:28] reserved
// ---------------------------------------------------------------------------
struct alignas(8) MessageHeader {
    uint32_t message_size;  // total wire size of this message (header + payload)
    uint32_t bits;          // packed fields (see layout above)
    uint64_t call_id;

    // Total size of the fixed base header (without optional fields).
    static constexpr size_t kBaseSize = 16;

    // Bit 1 of optionals nibble = has_endpoint_id.
    static constexpr uint32_t kOptEndpointId = 2u;

    // --- Accessors ---

    MessageType type() const noexcept {
        return static_cast<MessageType>(bits & 0x3Fu);
    }

    ChannelCode channel_code() const noexcept {
        return static_cast<ChannelCode>((bits >> 8) & 0xFFFFu);
    }

    uint32_t optionals() const noexcept {
        return (bits >> 24) & 0xFu;
    }

    bool has_endpoint_id() const noexcept {
        return (optionals() & kOptEndpointId) != 0;
    }

    // --- Mutators ---

    void set_type(MessageType t) noexcept {
        bits = (bits & ~0x3Fu) | (static_cast<uint32_t>(t) & 0x3Fu);
    }

    void set_channel_code(ChannelCode code) noexcept {
        bits = (bits & ~(0xFFFFu << 8)) | (static_cast<uint32_t>(code) << 8);
    }

    void set_optionals(uint32_t opt) noexcept {
        bits = (bits & ~(0xFu << 24)) | ((opt & 0xFu) << 24);
    }

    // --- Size helpers ---

    // Total size of the header section (fixed + optional fields).
    size_t header_size() const noexcept {
        size_t extra = has_endpoint_id() ? 32u : 0u;
        return kBaseSize + extra;
    }

    // Size of the payload (Writ document bytes) following the header.
    size_t payload_size() const noexcept {
        size_t hdr = header_size();
        if (static_cast<size_t>(message_size) <= hdr) return 0;
        return static_cast<size_t>(message_size) - hdr;
    }

    // --- Optional field: endpoint_id ---
    //
    // The endpoint_id bytes immediately follow the fixed 16-byte header.
    // Caller must ensure has_endpoint_id() is true before calling endpoint_id().

    EndpointID endpoint_id(const uint8_t* buf_after_header) const noexcept {
        EndpointID id{};
        std::memcpy(id.data(), buf_after_header, 32);
        return id;
    }

    void set_endpoint_id(uint8_t* buf_after_header, const EndpointID& id) noexcept {
        std::memcpy(buf_after_header, id.data(), 32);
    }
};

static_assert(sizeof(MessageHeader) == 16, "MessageHeader must be exactly 16 bytes");
static_assert(alignof(MessageHeader) == 8, "MessageHeader must be 8-byte aligned");

// ---------------------------------------------------------------------------
// Utility functions
// ---------------------------------------------------------------------------

// Fill an EndpointID with random bytes using <random>.
[[nodiscard]] inline logos::expected<EndpointID> make_random_endpoint_id() noexcept {
    EndpointID id{};
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;
    for (size_t i = 0; i < 4; ++i) {
        uint64_t chunk = dist(gen);
        std::memcpy(id.data() + i * 8, &chunk, 8);
    }
    return id;
}

// Derive a deterministic EndpointID from a human-readable name string.
// Uses FNV-1a 64-bit with four independent seeds to fill all 32 bytes.
// Both client and server call this with the same name to get the same ID.
// Convention: "package.ServiceName/method_name" (e.g. "echo.Echo/ping").
inline EndpointID endpoint_id_from_name(std::string_view name) noexcept {
    EndpointID id{};
    static constexpr uint64_t kFNVPrime = 0x100000001b3ULL;
    static constexpr uint64_t kSeeds[4] = {
        0xcbf29ce484222325ULL,   // FNV-1a standard offset basis
        0x9e3779b97f4a7c15ULL,   // Fibonacci hashing constant
        0x6c62272e07bb0142ULL,   // FNV-1a alternative
        0x517cc1b727220a95ULL,   // Knuth multiplicative hash
    };
    for (int s = 0; s < 4; ++s) {
        uint64_t h = kSeeds[s];
        for (uint8_t c : name) { h ^= c; h *= kFNVPrime; }
        std::memcpy(id.data() + s * 8, &h, 8);
    }
    return id;
}

// Convert an EndpointID to a lowercase hex string (64 hex chars).
[[nodiscard]] inline logos::expected<std::string> endpoint_id_to_hex(const EndpointID& id) noexcept {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (uint8_t byte : id) {
        out.push_back(kHex[byte >> 4]);
        out.push_back(kHex[byte & 0xF]);
    }
    return out;
}

} // namespace logos::hrpc
