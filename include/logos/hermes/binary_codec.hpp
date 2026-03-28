#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

#include <logos/hermes/document.hpp>

namespace logos::hermes {

// Binary streaming encoder/decoder for Hermes documents.
//
// The binary format is a dense, depth-first traversal of the object graph.
// Each value is prefixed with its TypeTag, followed by type-specific data:
//
//   Fixed-size primitives: TypeTag + raw bytes
//   Strings:               TypeTag + VarInt(length) + UTF-8 bytes
//   ObjectArray:           TypeTag + VarInt(size) + elements (recursive)
//   TinyObjectMap:         TypeTag + VarInt(size) + (key_byte, value)* (recursive)
//   ObjectMap:             TypeTag + VarInt(size) + (string_key, value)* (recursive)
//
// This format is distinct from the zero-copy arena format. It is designed for
// streaming over the wire — compact, self-describing, no pointer fixup needed.

// Encode a document to binary. Returns the encoded bytes.
std::vector<uint8_t> binary_encode(const HermesCtr& doc);

// Decode binary data into a new document.
HermesCtr binary_decode(const uint8_t* data, size_t size);

} // namespace logos::hermes
