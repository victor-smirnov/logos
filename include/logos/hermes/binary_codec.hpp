// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <vector>

#include <logos/hermes/document.hpp>
#include <logos/core/expected.hpp>

namespace logos::hermes {

// Portable binary serialization — a self-describing depth-first byte stream,
// independent of the in-memory arena layout (no pointer fixup on load). This is the
// TREE codec: it inlines each Ref's pointee, so it does NOT preserve shared
// subgraphs or cycles (use compactify() for graph-shaped data). Fine for the
// compiler's AST/LIR (trees).
//
// Each value is encoded as a kind byte (0 null / 1 Pod / 2 Ref); a Pod carries its
// 8-byte word, a Ref carries the pointee's varint TypeTag + a per-type body that
// recurses into child values.

[[nodiscard]] logos::expected<std::vector<uint8_t>>
binary_encode(const HermesCtr& doc) noexcept;

[[nodiscard]] logos::expected<HermesCtr>
binary_decode(const uint8_t* data, size_t size) noexcept;

} // namespace logos::hermes
