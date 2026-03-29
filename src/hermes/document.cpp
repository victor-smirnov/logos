// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/document.hpp>
#include <logos/verification/assert.hpp>

#include <cstring>
#include <unordered_map>

namespace logos::hermes {

// Deep copy engine — copies objects between arenas.
// All source pointers use src_base, all destination use dst Arena.
class DeepCopyState {
public:
    DeepCopyState(Arena& dst, const uint8_t* src_base)
        : dst_(dst), src_base_(src_base) {}

    void* copy_tagged_object(const void* src_obj) {
        if (!src_obj) return nullptr;

        auto it = copied_.find(src_obj);
        if (it != copied_.end()) return it->second;

        const auto* src_bytes = static_cast<const uint8_t*>(src_obj);
        TypeTag tag = TypeTag::read_before(src_bytes);
        uint64_t type_code = tag.type_code();

        void* dst_obj = nullptr;

        if (type_code == type_hash::Varchar) {
            dst_obj = copy_string(src_obj);
        } else if (type_code == type_hash::Varbinary) {
            dst_obj = copy_varbinary(src_obj);
        } else if (tag.descriptor() == TagDescriptor::Map && type_code == type_hash::Hermes) {
            dst_obj = copy_tiny_map(src_obj);
        } else if (tag.descriptor() == TagDescriptor::Array && type_code == type_hash::ObjectArray) {
            dst_obj = copy_object_array(src_obj);
        } else if (tag.descriptor() == TagDescriptor::Map && type_code == type_hash::ObjectMap) {
            dst_obj = copy_object_map(src_obj);
        } else {
            dst_obj = copy_fixed(src_obj, tag);
        }

        copied_[src_obj] = dst_obj;
        return dst_obj;
    }

    // Copy a TaggedPtr. For pointer mode, deep-copies the target.
    // src_slot is read using src_base_. dst_slot is written using dst base.
    void copy_tagged_ptr(const TaggedPtr* src_slot, TaggedPtr* dst_slot) {
        if (src_slot->is_null()) {
            *dst_slot = TaggedPtr{};
        } else if (src_slot->is_value()) {
            *dst_slot = *src_slot;
        } else {
            // Pointer mode: resolve src, deep-copy, set dst.
            const void* src_target = src_slot->as_ptr<void>(const_cast<uint8_t*>(src_base_));
            void* dst_target = copy_tagged_object(src_target);
            uint8_t* dst_base = dst_.head().data();
            dst_slot->set_pointer(dst_target, dst_base);
        }
    }

private:
    Arena& dst_;
    const uint8_t* src_base_;
    std::unordered_map<const void*, void*> copied_;

    void* copy_string(const void* src) {
        auto* s = static_cast<const ArenaString*>(src);
        return ArenaString::create(dst_, s->view());
    }

    void* copy_varbinary(const void* src) {
        auto* s = static_cast<const ArenaString*>(src);
        auto sv = s->view();
        uint8_t vlen_buf[8];
        size_t vlen_size = varint_encode(sv.size(), vlen_buf);
        size_t total = vlen_size + sv.size();
        TypeTag tag(type_hash::Varbinary, TagDescriptor::Data);
        auto* mem = static_cast<uint8_t*>(dst_.allocate(total, 2, tag));
        std::memcpy(mem, vlen_buf, vlen_size);
        std::memcpy(mem + vlen_size, sv.data(), sv.size());
        return mem;
    }

    void* copy_tiny_map(const void* src) {
        auto* src_map = static_cast<const TinyObjectMap*>(src);
        auto* dst_map = TinyObjectMap::create(dst_, src_map->capacity());

        uint8_t* dst_base = dst_.head().data();
        uint64_t bm = src_map->bitmap();
        for (uint8_t key = 0; key < TinyObjectMap::MAX_KEYS; ++key) {
            if (!(bm & (1ULL << key))) continue;

            dst_map->put(key, TaggedPtr{}, dst_);
            dst_base = dst_.head().data(); // re-derive after put

            TaggedPtr* dst_slot = dst_map->slot(key, dst_base);
            const TaggedPtr* src_slot = src_map->slot(key, const_cast<uint8_t*>(src_base_));

            copy_tagged_ptr(src_slot, dst_slot);
        }
        return dst_map;
    }

    void* copy_object_array(const void* src) {
        auto* src_arr = static_cast<const ObjectArray*>(src);
        auto* dst_arr = ObjectArray::create(dst_, src_arr->size());

        for (uint64_t i = 0; i < src_arr->size(); ++i) {
            dst_arr->push_back(TaggedPtr{}, dst_);
            uint8_t* dst_base = dst_.head().data();
            TaggedPtr* dst_slot = dst_arr->slot(i, dst_base);

            auto* src_arr_mut = const_cast<ObjectArray*>(src_arr);
            TaggedPtr* src_slot = src_arr_mut->slot(i, const_cast<uint8_t*>(src_base_));

            copy_tagged_ptr(src_slot, dst_slot);
        }
        return dst_arr;
    }

    void* copy_object_map(const void* src) {
        auto* src_map = static_cast<const ObjectMap*>(src);
        auto* dst_map = ObjectMap::create(dst_);

        src_map->for_each([&](ArenaString* src_key, TaggedPtr* src_val_slot) {
            dst_map->put(src_key->view(), TaggedPtr{}, dst_);
            uint8_t* dst_base = dst_.head().data();
            TaggedPtr* dst_slot = dst_map->get_slot(src_key->view(), dst_base);
            copy_tagged_ptr(src_val_slot, dst_slot);
        }, const_cast<uint8_t*>(src_base_));

        return dst_map;
    }

    void* copy_fixed(const void* src, TypeTag tag) {
        size_t size = fixed_type_size(tag.type_code());
        size_t alignment = fixed_type_alignment(tag.type_code());
        auto* mem = static_cast<uint8_t*>(dst_.allocate(size, alignment, tag));
        std::memcpy(mem, src, size);
        return mem;
    }

    static size_t fixed_type_size(uint64_t type_code) {
        switch (type_code) {
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

    static size_t fixed_type_alignment(uint64_t type_code) {
        switch (type_code) {
            case type_hash::TinyInt: case type_hash::UTinyInt: case type_hash::Boolean: return 2;
            case type_hash::SmallInt: case type_hash::USmallInt: return 2;
            case type_hash::Integer: case type_hash::UInteger: case type_hash::Real:
            case type_hash::Time: return 4;
            default: return 8;
        }
    }
};

HermesCtr HermesCtr::compactify() const {
    LOGOS_ASSERT(has_root(), "HERMES-DOC-001",
        "Cannot compactify a document without a root object");

    auto doc = HermesCtr::create(ArenaMode::GrowableSingleChunk, arena_->total_used() * 2);
    DeepCopyState state(doc.arena(), base());

    const void* src_root = header()->root.get(base());
    void* dst_root = state.copy_tagged_object(src_root);
    doc.set_root(dst_root);

    return doc;
}

HermesCtr HermesCtr::from_bytes(std::unique_ptr<uint8_t[]> data, size_t size) {
    HermesCtr doc;
    doc.arena_ = std::make_unique<Arena>(ArenaMode::GrowableSingleChunk, 0);

    auto& chunk = doc.arena_->head();
    chunk.memory = std::move(data);
    chunk.capacity = size;
    chunk.used = size;

    return doc;
}

HermesCtr HermesCtr::from_bytes_copy(const uint8_t* data, size_t size) {
    auto buf = std::make_unique<uint8_t[]>(size);
    std::memcpy(buf.get(), data, size);
    return from_bytes(std::move(buf), size);
}

} // namespace logos::hermes
