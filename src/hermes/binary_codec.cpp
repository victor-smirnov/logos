// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/binary_codec.hpp>
#include <logos/hermes/varint.hpp>
#include <logos/verification/assert.hpp>

#include <cstring>

namespace logos::hermes {

// ============================================================================
// Encoder
// ============================================================================

class BinaryEncoder {
public:
    void encode_document(const HermesCtr& doc) {
        LOGOS_ASSERT(doc.has_root(), "HERMES-BINARY-001",
            "Cannot encode a document without a root");

        // Encode root object from its arena location.
        const auto* root_bytes = static_cast<const uint8_t*>(doc.header()->root.get());
        encode_tagged_object(root_bytes);
    }

    std::vector<uint8_t>& output() { return buf_; }

private:
    std::vector<uint8_t> buf_;

    void write_bytes(const void* data, size_t len) {
        auto* p = static_cast<const uint8_t*>(data);
        buf_.insert(buf_.end(), p, p + len);
    }

    void write_varint(uint64_t value) {
        uint8_t tmp[8];
        size_t n = varint_encode(value, tmp);
        write_bytes(tmp, n);
    }

    void write_type_tag(TypeTag tag) {
        // Write the tag as variable-length bytes.
        size_t len = tag.byte_length();
        uint64_t raw = tag.raw();
        for (size_t i = 0; i < len; ++i) {
            buf_.push_back(static_cast<uint8_t>(raw >> (i * 8)));
        }
    }

    void encode_tagged_object(const uint8_t* obj_addr) {
        TypeTag tag = TypeTag::read_before(obj_addr);
        uint64_t tc = tag.type_code();

        if (tc == type_hash::Varchar) {
            encode_string(obj_addr, tag);
        } else if (tc == type_hash::Varbinary) {
            encode_string(obj_addr, tag); // Same layout as Varchar.
        } else if (tag.descriptor() == TagDescriptor::Map && tc == type_hash::Hermes) {
            encode_tiny_map(obj_addr, tag);
        } else if (tag.descriptor() == TagDescriptor::Array && tc == type_hash::ObjectArray) {
            encode_object_array(obj_addr, tag);
        } else if (tag.descriptor() == TagDescriptor::Map && tc == type_hash::ObjectMap) {
            encode_object_map(obj_addr, tag);
        } else {
            encode_fixed(obj_addr, tag);
        }
    }

    void encode_tagged_ptr(const TaggedPtr* slot) {
        if (slot->is_null()) {
            // Encode null as a zero-length special marker.
            // Use type_code 0 with code_len 0 as null indicator.
            buf_.push_back(0);
            return;
        }

        if (slot->is_value()) {
            // Embedded value — encode as the appropriate fixed type.
            uint8_t th = slot->value_type_hash();
            TypeTag tag(th, TagDescriptor::Data);
            write_type_tag(tag);

            // Write the value bytes (extract from the tagged ptr).
            size_t sz = embedded_value_size(th);
            // Copy the value bytes directly from the TaggedPtr's storage.
            uint64_t raw = slot->raw();
            write_bytes(&raw, sz);
            return;
        }

        // Pointer mode — encode the pointed-to object.
        auto* target = slot->as_ptr<uint8_t>();
        encode_tagged_object(target);
    }

    void encode_string(const uint8_t* obj, TypeTag tag) {
        auto* s = reinterpret_cast<const ArenaString*>(obj);
        auto sv = s->view();

        write_type_tag(tag);
        write_varint(sv.size());
        write_bytes(sv.data(), sv.size());
    }

    void encode_tiny_map(const uint8_t* obj, TypeTag tag) {
        auto* map = reinterpret_cast<const TinyObjectMap*>(obj);

        write_type_tag(tag);
        write_varint(map->size());

        uint64_t bm = map->bitmap();
        for (uint8_t key = 0; key < TinyObjectMap::MAX_KEYS; ++key) {
            if (!(bm & (1ULL << key))) continue;
            buf_.push_back(key); // Key as single byte.
            const TaggedPtr* slot = map->slot(key);
            encode_tagged_ptr(slot);
        }
    }

    void encode_object_array(const uint8_t* obj, TypeTag tag) {
        auto* arr = reinterpret_cast<const ObjectArray*>(obj);

        write_type_tag(tag);
        write_varint(arr->size());

        // Const cast needed because slot() is non-const.
        auto* arr_mut = const_cast<ObjectArray*>(arr);
        for (uint64_t i = 0; i < arr->size(); ++i) {
            TaggedPtr* slot = arr_mut->slot(i);
            encode_tagged_ptr(slot);
        }
    }

    void encode_object_map(const uint8_t* obj, TypeTag tag) {
        auto* map = reinterpret_cast<const ObjectMap*>(obj);

        write_type_tag(tag);
        write_varint(map->size());

        map->for_each([&](ArenaString* key, TaggedPtr* val_slot) {
            // Encode key as string (without TypeTag — always Varchar by convention).
            auto sv = key->view();
            write_varint(sv.size());
            write_bytes(sv.data(), sv.size());

            // Encode value.
            encode_tagged_ptr(val_slot);
        });
    }

    void encode_fixed(const uint8_t* obj, TypeTag tag) {
        size_t sz = fixed_size_for(tag.type_code());
        write_type_tag(tag);
        write_bytes(obj, sz);
    }

    static size_t fixed_size_for(uint64_t tc) {
        switch (tc) {
            case type_hash::TinyInt: case type_hash::UTinyInt: case type_hash::Boolean: return 1;
            case type_hash::SmallInt: case type_hash::USmallInt: return 2;
            case type_hash::Integer: case type_hash::UInteger: case type_hash::Real:
            case type_hash::Time: return 4;
            case type_hash::BigInt: case type_hash::UBigInt: case type_hash::Double:
            case type_hash::Timestamp: case type_hash::TimestampWithTZ:
            case type_hash::Date: case type_hash::TimeWithTZ:
            case type_hash::Uid64: return 8;
            case type_hash::Uid128: return 16;
            case type_hash::Uid256: return 32;
            default: return 8;
        }
    }

    static size_t embedded_value_size(uint8_t type_hash) {
        switch (type_hash) {
            case type_hash::TinyInt: case type_hash::UTinyInt: case type_hash::Boolean: return 1;
            case type_hash::SmallInt: case type_hash::USmallInt: return 2;
            case type_hash::Integer: case type_hash::UInteger: case type_hash::Real:
            case type_hash::Time: return 4;
            default: return 4;
        }
    }
};

// ============================================================================
// Decoder
// ============================================================================

class BinaryDecoder {
public:
    BinaryDecoder(const uint8_t* data, size_t size)
        : data_(data), size_(size), pos_(0) {}

    HermesCtr decode() {
        auto doc = HermesCtr::create();
        void* root = decode_tagged_object(doc.arena());
        doc.set_root_offset(doc.offset_of(root));
        return doc;
    }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_;

    uint8_t read_byte() {
        LOGOS_ASSERT(pos_ < size_, "HERMES-BINARY-002", "Unexpected end of binary data");
        return data_[pos_++];
    }

    void read_bytes(void* dst, size_t n) {
        LOGOS_ASSERT(pos_ + n <= size_, "HERMES-BINARY-002",
            "Unexpected end of binary data (need {} bytes, have {})", n, size_ - pos_);
        std::memcpy(dst, data_ + pos_, n);
        pos_ += n;
    }

    uint64_t read_varint() {
        VarIntResult r = varint_decode(data_ + pos_);
        pos_ += r.bytes_read;
        return r.value;
    }

    TypeTag read_type_tag() {
        uint8_t first = data_[pos_];
        uint8_t code_len = first & 0x07;
        uint64_t val = 0;
        for (size_t i = 0; i <= static_cast<size_t>(code_len); ++i) {
            val |= static_cast<uint64_t>(data_[pos_ + i]) << (i * 8);
        }
        pos_ += code_len + 1;
        return TypeTag::from_raw(val);
    }

    void* decode_tagged_object(Arena& arena) {
        TypeTag tag = read_type_tag();
        uint64_t tc = tag.type_code();

        if (tc == type_hash::Varchar) {
            return decode_string(arena, tag);
        } else if (tc == type_hash::Varbinary) {
            return decode_string(arena, tag);
        } else if (tag.descriptor() == TagDescriptor::Map && tc == type_hash::Hermes) {
            return decode_tiny_map(arena);
        } else if (tag.descriptor() == TagDescriptor::Array && tc == type_hash::ObjectArray) {
            return decode_object_array(arena);
        } else if (tag.descriptor() == TagDescriptor::Map && tc == type_hash::ObjectMap) {
            return decode_object_map(arena);
        } else {
            return decode_fixed(arena, tag);
        }
    }

    // Decode a TaggedPtr element into a slot in-place.
    void decode_tagged_ptr(Arena& arena, TaggedPtr* dst_slot) {
        uint8_t first = data_[pos_];

        // Null check: a zero byte means null.
        if (first == 0) {
            ++pos_;
            *dst_slot = TaggedPtr{};
            return;
        }

        // Parse type tag to determine what this is.
        TypeTag tag = read_type_tag();
        uint64_t tc = tag.type_code();

        // Check if this is an embeddable type.
        bool is_embed = (tc < 128) && is_embeddable_by_hash(tc);

        if (is_embed) {
            // Read value bytes and construct embedded TaggedPtr.
            size_t sz = embedded_value_size_for(tc);
            uint64_t bits = 0;
            read_bytes(&bits, sz);
            auto* bytes = reinterpret_cast<uint8_t*>(&bits);
            bytes[7] = static_cast<uint8_t>((tc << 1) | 1);
            *dst_slot = TaggedPtr::from_raw(bits);
        } else {
            // Non-embeddable: decode as arena object and store pointer.
            // We need to re-read the tag, but we already consumed it.
            // Re-dispatch based on tag.
            void* obj = decode_object_from_tag(arena, tag);
            dst_slot->set_pointer(obj);
        }
    }

    void* decode_object_from_tag(Arena& arena, TypeTag tag) {
        uint64_t tc = tag.type_code();

        if (tc == type_hash::Varchar || tc == type_hash::Varbinary) {
            return decode_string(arena, tag);
        } else if (tag.descriptor() == TagDescriptor::Map && tc == type_hash::Hermes) {
            return decode_tiny_map(arena);
        } else if (tag.descriptor() == TagDescriptor::Array && tc == type_hash::ObjectArray) {
            return decode_object_array(arena);
        } else if (tag.descriptor() == TagDescriptor::Map && tc == type_hash::ObjectMap) {
            return decode_object_map(arena);
        } else {
            return decode_fixed(arena, tag);
        }
    }

    void* decode_string(Arena& arena, TypeTag tag) {
        uint64_t len = read_varint();
        LOGOS_ASSERT(pos_ + len <= size_, "HERMES-BINARY-002", "String data exceeds buffer");

        std::string_view sv(reinterpret_cast<const char*>(data_ + pos_), len);
        pos_ += len;

        // Allocate in arena with the proper tag (Varchar or Varbinary).
        uint8_t vlen_buf[8];
        size_t vlen_size = varint_encode(len, vlen_buf);
        size_t total = vlen_size + len;
        auto* mem = static_cast<uint8_t*>(arena.allocate(total, 2, tag));
        std::memcpy(mem, vlen_buf, vlen_size);
        std::memcpy(mem + vlen_size, sv.data(), len);
        return mem;
    }

    void* decode_tiny_map(Arena& arena) {
        uint64_t count = read_varint();
        auto* map = TinyObjectMap::create(arena, static_cast<uint8_t>(count));

        for (uint64_t i = 0; i < count; ++i) {
            uint8_t key = read_byte();
            map->put(key, TaggedPtr{}, arena);
            TaggedPtr* slot = map->slot(key);
            decode_tagged_ptr(arena, slot);
        }
        return map;
    }

    void* decode_object_array(Arena& arena) {
        uint64_t count = read_varint();
        auto* arr = ObjectArray::create(arena, count);

        for (uint64_t i = 0; i < count; ++i) {
            arr->push_back(TaggedPtr{}, arena);
            TaggedPtr* slot = arr->slot(i);
            decode_tagged_ptr(arena, slot);
        }
        return arr;
    }

    void* decode_object_map(Arena& arena) {
        uint64_t count = read_varint();
        auto* map = ObjectMap::create(arena);

        for (uint64_t i = 0; i < count; ++i) {
            // Read key string (no TypeTag prefix — always Varchar by convention).
            uint64_t key_len = read_varint();
            std::string_view key(reinterpret_cast<const char*>(data_ + pos_), key_len);
            pos_ += key_len;

            // Put placeholder, then decode value into slot.
            map->put(key, TaggedPtr{}, arena);
            TaggedPtr* slot = map->get_slot(key);
            decode_tagged_ptr(arena, slot);
        }
        return map;
    }

    void* decode_fixed(Arena& arena, TypeTag tag) {
        size_t sz = fixed_size_for(tag.type_code());
        size_t align = fixed_align_for(tag.type_code());
        auto* mem = static_cast<uint8_t*>(arena.allocate(sz, align, tag));
        read_bytes(mem, sz);
        return mem;
    }

    static bool is_embeddable_by_hash(uint64_t tc) {
        switch (tc) {
            case type_hash::TinyInt: case type_hash::UTinyInt:
            case type_hash::SmallInt: case type_hash::USmallInt:
            case type_hash::Integer: case type_hash::UInteger:
            case type_hash::Real: case type_hash::Time:
            case type_hash::Boolean:
                return true;
            default:
                return false;
        }
    }

    static size_t embedded_value_size_for(uint64_t tc) {
        switch (tc) {
            case type_hash::TinyInt: case type_hash::UTinyInt: case type_hash::Boolean: return 1;
            case type_hash::SmallInt: case type_hash::USmallInt: return 2;
            case type_hash::Integer: case type_hash::UInteger: case type_hash::Real:
            case type_hash::Time: return 4;
            default: return 4;
        }
    }

    static size_t fixed_size_for(uint64_t tc) {
        switch (tc) {
            case type_hash::TinyInt: case type_hash::UTinyInt: case type_hash::Boolean: return 1;
            case type_hash::SmallInt: case type_hash::USmallInt: return 2;
            case type_hash::Integer: case type_hash::UInteger: case type_hash::Real:
            case type_hash::Time: return 4;
            case type_hash::BigInt: case type_hash::UBigInt: case type_hash::Double:
            case type_hash::Timestamp: case type_hash::TimestampWithTZ:
            case type_hash::Date: case type_hash::TimeWithTZ:
            case type_hash::Uid64: return 8;
            case type_hash::Uid128: return 16;
            case type_hash::Uid256: return 32;
            default: return 8;
        }
    }

    static size_t fixed_align_for(uint64_t tc) {
        switch (tc) {
            case type_hash::TinyInt: case type_hash::UTinyInt: case type_hash::Boolean: return 2;
            case type_hash::SmallInt: case type_hash::USmallInt: return 2;
            case type_hash::Integer: case type_hash::UInteger: case type_hash::Real:
            case type_hash::Time: return 4;
            default: return 8;
        }
    }
};

// ============================================================================
// Public API
// ============================================================================

std::vector<uint8_t> binary_encode(const HermesCtr& doc) {
    BinaryEncoder encoder;
    encoder.encode_document(doc);
    return std::move(encoder.output());
}

HermesCtr binary_decode(const uint8_t* data, size_t size) {
    BinaryDecoder decoder(data, size);
    return decoder.decode();
}

} // namespace logos::hermes
