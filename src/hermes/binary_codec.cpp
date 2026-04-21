// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/binary_codec.hpp>
#include <logos/hermes/access.hpp>
#include <logos/hermes/varint.hpp>
#include <logos/verification/assert.hpp>

#include <cstring>

namespace logos::hermes {

// ============================================================================
// Encoder
// ============================================================================

class BinaryEncoder {
public:
    void encode_document(const Hermes& doc) noexcept {
        LOGOS_ASSERT(doc.has_root(), "HERMES-BINARY-001",
            "Cannot encode a document without a root");

        base_ = HermesAccess::base(doc);

        // Encode root object from its arena location.
        const auto* root_bytes = static_cast<const uint8_t*>(HermesAccess::root<void>(doc));
        encode_tagged_object(root_bytes);
    }

    std::vector<uint8_t>& output() { return buf_; }

private:
    std::vector<uint8_t> buf_;
    const uint8_t* base_ = nullptr;

    void write_bytes(const void* data, size_t len) noexcept {
        auto* p = static_cast<const uint8_t*>(data);
        buf_.insert(buf_.end(), p, p + len);
    }

    void write_varint(uint64_t value) noexcept {
        uint8_t tmp[8];
        size_t n = varint_encode(value, tmp);
        write_bytes(tmp, n);
    }

    void write_type_tag(TypeTag tag) noexcept {
        // Logos byte-direct encoding (stream order): header byte first, then
        // little-endian code bytes for multi-byte tags.
        uint64_t tc = tag.type_code();
        if (tc == 0) return;
        if (tc <= 222) {
            buf_.push_back(static_cast<uint8_t>(tc));
            return;
        }
        size_t n = 0;
        uint64_t v = tc;
        while (v > 0) { v >>= 8; ++n; }
        if (n > 8) n = 8;
        buf_.push_back(static_cast<uint8_t>(222 + n));
        for (size_t i = 0; i < n; ++i) {
            buf_.push_back(static_cast<uint8_t>(tc >> (i * 8)));
        }
    }

    void encode_tagged_object(const uint8_t* obj_addr) noexcept {
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

    void encode_tagged_ptr(const AnyVal* slot) noexcept {
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

            // AnyVal stores a 24-bit payload in bits[31:8]; extract and write
            // the low `sz` bytes of it.
            size_t sz = embedded_value_size(th);
            uint32_t payload24 = (slot->raw() >> 8) & 0x00FFFFFFu;
            write_bytes(&payload24, sz);
            return;
        }

        // Pointer mode — encode the pointed-to object.
        auto* target = slot->as_ptr<uint8_t>(base_);
        encode_tagged_object(target);
    }

    void encode_string(const uint8_t* obj, TypeTag tag) noexcept {
        auto* s = reinterpret_cast<const ArenaString*>(obj);
        auto sv = s->view();

        write_type_tag(tag);
        write_varint(sv.size());
        write_bytes(sv.data(), sv.size());
    }

    void encode_tiny_map(const uint8_t* obj, TypeTag tag) noexcept {
        auto* map = reinterpret_cast<const TinyObjectMap*>(obj);

        write_type_tag(tag);
        write_varint(map->size());

        uint64_t bm = map->bitmap();
        for (uint8_t key = 0; key < TinyObjectMap::MAX_KEYS; ++key) {
            if (!(bm & (1ULL << key))) continue;
            buf_.push_back(key); // Key as single byte.
            const AnyVal* s = map->slot(key, base_);
            encode_tagged_ptr(s);
        }
    }

    void encode_object_array(const uint8_t* obj, TypeTag tag) noexcept {
        auto* arr = reinterpret_cast<const ObjectArray*>(obj);

        write_type_tag(tag);
        write_varint(arr->size());

        // Const cast needed because slot() is non-const.
        auto* arr_mut = const_cast<ObjectArray*>(arr);
        uint8_t* base_mut = const_cast<uint8_t*>(base_);
        for (uint64_t i = 0; i < arr->size(); ++i) {
            AnyVal* s = arr_mut->slot(i, base_mut);
            encode_tagged_ptr(s);
        }
    }

    void encode_object_map(const uint8_t* obj, TypeTag tag) noexcept {
        auto* map = reinterpret_cast<const ObjectMap*>(obj);

        write_type_tag(tag);
        write_varint(map->size());

        uint8_t* base_mut = const_cast<uint8_t*>(base_);
        map->for_each([&](ArenaString* key, AnyVal* val_slot) noexcept {
            // Encode key as string (without TypeTag — always Varchar by convention).
            auto sv = key->view();
            write_varint(sv.size());
            write_bytes(sv.data(), sv.size());

            // Encode value.
            encode_tagged_ptr(val_slot);
        }, base_mut);
    }

    void encode_fixed(const uint8_t* obj, TypeTag tag) noexcept {
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
            case type_hash::Integer: case type_hash::UInteger:
            case type_hash::Time: return 3;  // 24-bit payload
            default: return 3;
        }
    }
};

// ============================================================================
// Decoder
// ============================================================================

class BinaryDecoder {
public:
    BinaryDecoder(const uint8_t* data, size_t size) noexcept
        : data_(data), size_(size), pos_(0) {}

    logos::expected<Hermes> decode() noexcept {
        LOGOS_TRY(auto doc, make_doc());
        LOGOS_TRY(auto* root, decode_tagged_object(HermesAccess::arena(doc)));
        HermesAccess::set_root_offset(doc, HermesAccess::offset_of(doc, root));
        return doc;
    }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_;

    uint8_t read_byte() noexcept {
        LOGOS_ASSERT(pos_ < size_, "HERMES-BINARY-002", "Unexpected end of binary data");
        return data_[pos_++];
    }

    void read_bytes(void* dst, size_t n) noexcept {
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

    TypeTag read_type_tag() noexcept {
        LOGOS_ASSERT(pos_ < size_, "HERMES-BINARY-002",
            "Unexpected end of binary data reading TypeTag header");
        uint8_t first = data_[pos_++];
        if (first == 0) return TypeTag{};
        if (first <= 222) return TypeTag::from_raw(first);
        LOGOS_ASSERT(first <= 230, "HERMES-BINARY-002",
            "Invalid TypeTag header byte {} (> 230)", first);
        size_t n = static_cast<size_t>(first - 222);
        LOGOS_ASSERT(pos_ + n <= size_, "HERMES-BINARY-002",
            "Unexpected end of binary data reading TypeTag code bytes");
        uint64_t val = 0;
        for (size_t i = 0; i < n; ++i) {
            val |= static_cast<uint64_t>(data_[pos_ + i]) << (i * 8);
        }
        pos_ += n;
        return TypeTag::from_raw(val);
    }

    logos::expected<void*> decode_tagged_object(Arena& arena) noexcept {
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

    // Decode a AnyVal element into a slot in-place.
    logos::expected<void> decode_tagged_ptr(Arena& arena, AnyVal* dst_slot) noexcept {
        uint8_t first = data_[pos_];

        // Null check: a zero byte means null.
        if (first == 0) {
            ++pos_;
            *dst_slot = AnyVal{};
            return {};
        }

        // Parse type tag to determine what this is.
        TypeTag tag = read_type_tag();
        uint64_t tc = tag.type_code();

        // Check if this is an embeddable type.
        bool is_embed = (tc < 128) && is_embeddable_by_hash(tc);

        if (is_embed) {
            // Read up to sz payload bytes, reconstruct 24-bit payload, and
            // pack into a 4-byte AnyVal.
            size_t sz = embedded_value_size_for(tc);
            uint32_t payload24 = 0;
            read_bytes(&payload24, sz);
            payload24 &= 0x00FFFFFFu;
            uint32_t bits = (payload24 << 8)
                          | (static_cast<uint32_t>(tc) << 1)
                          | 1u;
            *dst_slot = AnyVal::from_raw(bits);
        } else {
            // Non-embeddable: decode as arena object and store pointer.
            LOGOS_TRY(auto* obj, decode_object_from_tag(arena, tag));
            uint8_t* base = arena.head().data();
            dst_slot->set_pointer(obj, base);
        }
        return {};
    }

    logos::expected<void*> decode_object_from_tag(Arena& arena, TypeTag tag) noexcept {
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

    logos::expected<void*> decode_string(Arena& arena, TypeTag tag) noexcept {
        uint64_t len = read_varint();
        LOGOS_ASSERT(pos_ + len <= size_, "HERMES-BINARY-002", "String data exceeds buffer");

        std::string_view sv(reinterpret_cast<const char*>(data_ + pos_), len);
        pos_ += len;

        // Allocate in arena with the proper tag (Varchar or Varbinary).
        uint8_t vlen_buf[8];
        size_t vlen_size = varint_encode(len, vlen_buf);
        size_t total = vlen_size + len;
        LOGOS_TRY(auto* mem_void, arena.allocate(total, 2, tag));
        auto* mem = static_cast<uint8_t*>(mem_void);
        std::memcpy(mem, vlen_buf, vlen_size);
        std::memcpy(mem + vlen_size, sv.data(), len);
        return mem;
    }

    logos::expected<void*> decode_tiny_map(Arena& arena) noexcept {
        uint64_t count = read_varint();
        LOGOS_TRY(auto* map, TinyObjectMap::create(arena, static_cast<uint8_t>(count)));

        for (uint64_t i = 0; i < count; ++i) {
            uint8_t key = read_byte();
            LOGOS_TRY_VOID(map->put(key, AnyVal{}, arena));
            uint8_t* base = arena.head().data();
            AnyVal* s = map->slot(key, base);
            LOGOS_TRY_VOID(decode_tagged_ptr(arena, s));
        }
        return map;
    }

    logos::expected<void*> decode_object_array(Arena& arena) noexcept {
        uint64_t count = read_varint();
        LOGOS_TRY(auto* arr, ObjectArray::create(arena, count));

        for (uint64_t i = 0; i < count; ++i) {
            LOGOS_TRY_VOID(arr->push_back(AnyVal{}, arena));
            uint8_t* base = arena.head().data();
            AnyVal* s = arr->slot(i, base);
            LOGOS_TRY_VOID(decode_tagged_ptr(arena, s));
        }
        return arr;
    }

    logos::expected<void*> decode_object_map(Arena& arena) noexcept {
        uint64_t count = read_varint();
        LOGOS_TRY(auto* map, ObjectMap::create(arena));

        for (uint64_t i = 0; i < count; ++i) {
            // Read key string (no TypeTag prefix — always Varchar by convention).
            uint64_t key_len = read_varint();
            std::string_view key(reinterpret_cast<const char*>(data_ + pos_), key_len);
            pos_ += key_len;

            // Put placeholder, then decode value into slot.
            LOGOS_TRY_VOID(map->put(key, AnyVal{}, arena));
            uint8_t* base = arena.head().data();
            AnyVal* s = map->get_slot(key, base);
            LOGOS_TRY_VOID(decode_tagged_ptr(arena, s));
        }
        return map;
    }

    logos::expected<void*> decode_fixed(Arena& arena, TypeTag tag) noexcept {
        size_t sz = fixed_size_for(tag.type_code());
        size_t align = fixed_align_for(tag.type_code());
        LOGOS_TRY(auto* mem_void, arena.allocate(sz, align, tag));
        auto* mem = static_cast<uint8_t*>(mem_void);
        read_bytes(mem, sz);
        return mem;
    }

    static bool is_embeddable_by_hash(uint64_t tc) noexcept{
        switch (tc) {
            case type_hash::TinyInt: case type_hash::UTinyInt:
            case type_hash::SmallInt: case type_hash::USmallInt:
            case type_hash::Integer: case type_hash::UInteger:
            case type_hash::Time:
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
            case type_hash::Integer: case type_hash::UInteger:
            case type_hash::Time: return 3;
            default: return 3;
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

logos::expected<std::vector<uint8_t>> binary_encode(const Hermes& doc) noexcept {
    BinaryEncoder encoder;
    encoder.encode_document(doc);
    return std::move(encoder.output());
}

logos::expected<Hermes> binary_decode(const uint8_t* data, size_t size) noexcept {
    BinaryDecoder decoder(data, size);
    return decoder.decode();
}

} // namespace logos::hermes
