// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/document.hpp>
#include <logos/verification/assert.hpp>

#include <cstring>
#include <unordered_map>

namespace logos::hermes {

// ============================================================================
// Deep copy engine
// ============================================================================

// DeepCopyState tracks already-copied objects to handle shared references
// and avoid duplicating the same arena object.
class DeepCopyState {
public:
    DeepCopyState(Arena& dst) : dst_(dst) {}

    // Copy a tagged arena object. Returns pointer to the copy in the destination arena.
    // If the object was already copied, returns the cached copy.
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
            // Fixed-size data type — copy raw bytes based on known sizes.
            dst_obj = copy_fixed(src_obj, tag);
        }

        copied_[src_obj] = dst_obj;
        return dst_obj;
    }

    // Copy a TaggedPtr value. For pointer mode, deep-copies the target and
    // writes the new pointer in-place at dst_slot.
    void copy_tagged_ptr(const TaggedPtr* src_slot, TaggedPtr* dst_slot) {
        if (src_slot->is_null()) {
            *dst_slot = TaggedPtr{};
        } else if (src_slot->is_value()) {
            // Embedded values are self-contained — copy bits directly.
            *dst_slot = *src_slot;
        } else {
            // Pointer mode: deep-copy target, then set pointer in-place.
            const void* src_target = src_slot->as_ptr<void>();
            void* dst_target = copy_tagged_object(src_target);
            dst_slot->set_pointer(dst_target);
        }
    }

private:
    Arena& dst_;
    std::unordered_map<const void*, void*> copied_;

    void* copy_string(const void* src) {
        auto* s = static_cast<const ArenaString*>(src);
        auto* copy = ArenaString::create(dst_, s->view());
        return copy;
    }

    void* copy_varbinary(const void* src) {
        // Varbinary has the same layout as ArenaString (vlen + bytes).
        auto* s = static_cast<const ArenaString*>(src);
        auto sv = s->view();
        // Allocate with Varbinary tag.
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

        uint64_t bm = src_map->bitmap();
        for (uint8_t key = 0; key < TinyObjectMap::MAX_KEYS; ++key) {
            if (!(bm & (1ULL << key))) continue;

            // Put a null placeholder to establish the slot.
            dst_map->put(key, TaggedPtr{}, dst_);
            TaggedPtr* dst_slot = dst_map->slot(key);
            const TaggedPtr* src_slot = src_map->slot(key);

            copy_tagged_ptr(src_slot, dst_slot);
        }
        return dst_map;
    }

    void* copy_object_array(const void* src) {
        auto* src_arr = static_cast<const ObjectArray*>(src);
        auto* dst_arr = ObjectArray::create(dst_, src_arr->size());

        for (uint64_t i = 0; i < src_arr->size(); ++i) {
            // Push null placeholder.
            dst_arr->push_back(TaggedPtr{}, dst_);
            TaggedPtr* dst_slot = dst_arr->slot(i);

            // We need the source slot address for pointer-mode entries.
            // src_arr->get(i) returns by value — for embedded values, that's fine.
            // For pointer-mode, we need the slot address.
            // ObjectArray has slot(), but it's non-const. We'll cast.
            auto* src_mut = const_cast<ObjectArray*>(src_arr);
            TaggedPtr* src_slot = src_mut->slot(i);

            copy_tagged_ptr(src_slot, dst_slot);
        }
        return dst_arr;
    }

    void* copy_object_map(const void* src) {
        auto* src_map = static_cast<const ObjectMap*>(src);
        auto* dst_map = ObjectMap::create(dst_);

        src_map->for_each([&](ArenaString* src_key, TaggedPtr* src_val_slot) {
            // Put a null placeholder to establish the slot, then deep-copy the value.
            dst_map->put(src_key->view(), TaggedPtr{}, dst_);
            TaggedPtr* dst_slot = dst_map->get_slot(src_key->view());
            copy_tagged_ptr(src_val_slot, dst_slot);
        });

        return dst_map;
    }

    void* copy_fixed(const void* src, TypeTag tag) {
        // Determine size from known type hashes.
        size_t size = fixed_type_size(tag.type_code());
        size_t alignment = fixed_type_alignment(tag.type_code());

        auto* mem = static_cast<uint8_t*>(dst_.allocate(size, alignment, tag));
        std::memcpy(mem, src, size);
        return mem;
    }

    static size_t fixed_type_size(uint64_t type_code) {
        switch (type_code) {
            case type_hash::TinyInt:   case type_hash::UTinyInt:  case type_hash::Boolean: return 1;
            case type_hash::SmallInt:  case type_hash::USmallInt: return 2;
            case type_hash::Integer:   case type_hash::UInteger:  case type_hash::Real:
            case type_hash::Time: return 4;
            case type_hash::BigInt:    case type_hash::UBigInt:   case type_hash::Double:
            case type_hash::Timestamp: case type_hash::TimestampWithTZ:
            case type_hash::Date:      case type_hash::TimeWithTZ:
            case type_hash::Uid64: return 8;
            case type_hash::Uid128: return 16;
            case type_hash::Uid256: return 32;
            default: return 8; // Fallback for unknown types.
        }
    }

    static size_t fixed_type_alignment(uint64_t type_code) {
        switch (type_code) {
            case type_hash::TinyInt:   case type_hash::UTinyInt:  case type_hash::Boolean: return 2; // min 2 for tags
            case type_hash::SmallInt:  case type_hash::USmallInt: return 2;
            case type_hash::Integer:   case type_hash::UInteger:  case type_hash::Real:
            case type_hash::Time: return 4;
            default: return 8;
        }
    }
};

// ============================================================================
// HermesCtr implementation
// ============================================================================

HermesCtr HermesCtr::compactify() const {
    LOGOS_ASSERT(has_root(), "HERMES-DOC-001",
        "Cannot compactify a document without a root object");

    auto doc = HermesCtr::create(ArenaMode::GrowableSingleChunk, arena_->total_used() * 2);
    DeepCopyState state(doc.arena());

    const void* src_root = header()->root.get();
    void* dst_root = state.copy_tagged_object(src_root);
    doc.set_root_offset(doc.offset_of(dst_root));

    return doc;
}

HermesCtr HermesCtr::from_bytes(std::unique_ptr<uint8_t[]> data, size_t size) {
    HermesCtr doc;
    doc.arena_ = std::make_unique<Arena>(ArenaMode::GrowableSingleChunk, 0);

    // Replace the arena's chunk with the provided data.
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
