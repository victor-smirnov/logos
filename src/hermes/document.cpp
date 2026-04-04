// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/document.hpp>
#include <logos/hermes/access.hpp>
#include <logos/verification/assert.hpp>

#include <cstring>
#include <unordered_map>

namespace logos::hermes {

// Deep copy engine — copies objects between arenas.
class DeepCopyState {
public:
    DeepCopyState(Arena& dst, const uint8_t* src_base)
        : dst_(dst), src_base_(src_base) {}

    logos::expected<void*> copy_tagged_object(const void* src_obj) noexcept {
        if (!src_obj) return nullptr;

        auto it = copied_.find(src_obj);
        if (it != copied_.end()) return it->second;

        const auto* src_bytes = static_cast<const uint8_t*>(src_obj);
        TypeTag tag = TypeTag::read_before(src_bytes);
        uint64_t type_code = tag.type_code();

        void* dst_obj = nullptr;

        if (type_code == type_hash::Varchar) {
            LOGOS_TRY(dst_obj, copy_string(src_obj));
        } else if (type_code == type_hash::Varbinary) {
            LOGOS_TRY(dst_obj, copy_varbinary(src_obj));
        } else if (tag.descriptor() == TagDescriptor::Map && type_code == type_hash::Hermes) {
            LOGOS_TRY(dst_obj, copy_tiny_map(src_obj));
        } else if (tag.descriptor() == TagDescriptor::Array && type_code == type_hash::ObjectArray) {
            LOGOS_TRY(dst_obj, copy_object_array(src_obj));
        } else if (tag.descriptor() == TagDescriptor::Map && type_code == type_hash::ObjectMap) {
            LOGOS_TRY(dst_obj, copy_object_map(src_obj));
        } else {
            LOGOS_TRY(dst_obj, copy_fixed(src_obj, tag));
        }

        copied_[src_obj] = dst_obj;
        return dst_obj;
    }

    logos::expected<void> copy_tagged_ptr(const AnyVal* src_slot, AnyVal* dst_slot) noexcept {
        if (src_slot->is_null()) {
            *dst_slot = AnyVal{};
        } else if (src_slot->is_value()) {
            *dst_slot = *src_slot;
        } else {
            const void* src_target = src_slot->as_ptr<void>(const_cast<uint8_t*>(src_base_));
            LOGOS_TRY(auto* dst_target, copy_tagged_object(src_target));
            uint8_t* dst_base = dst_.head().data();
            dst_slot->set_pointer(dst_target, dst_base);
        }
        return {};
    }

private:
    Arena& dst_;
    const uint8_t* src_base_;
    std::unordered_map<const void*, void*> copied_;

    logos::expected<void*> copy_string(const void* src) noexcept {
        auto* s = static_cast<const ArenaString*>(src);
        LOGOS_TRY(auto* result, ArenaString::create(dst_, s->view()));
        return result;
    }

    logos::expected<void*> copy_varbinary(const void* src) noexcept {
        auto* s = static_cast<const ArenaString*>(src);
        auto sv = s->view();
        uint8_t vlen_buf[8];
        size_t vlen_size = varint_encode(sv.size(), vlen_buf);
        size_t total = vlen_size + sv.size();
        TypeTag tag(type_hash::Varbinary, TagDescriptor::Data);
        LOGOS_TRY(auto* mem_void, dst_.allocate(total, 2, tag));
        auto* mem = static_cast<uint8_t*>(mem_void);
        std::memcpy(mem, vlen_buf, vlen_size);
        std::memcpy(mem + vlen_size, sv.data(), sv.size());
        return mem;
    }

    logos::expected<void*> copy_tiny_map(const void* src) noexcept {
        auto* src_map = static_cast<const TinyObjectMap*>(src);
        LOGOS_TRY(auto* dst_map, TinyObjectMap::create(dst_, src_map->capacity()));

        uint8_t* dst_base = dst_.head().data();
        uint64_t bm = src_map->bitmap();
        for (uint8_t key = 0; key < TinyObjectMap::MAX_KEYS; ++key) {
            if (!(bm & (1ULL << key))) continue;

            LOGOS_TRY_VOID(dst_map->put(key, AnyVal{}, dst_));
            dst_base = dst_.head().data();

            AnyVal* dst_slot = dst_map->slot(key, dst_base);
            const AnyVal* src_slot = src_map->slot(key, const_cast<uint8_t*>(src_base_));

            LOGOS_TRY_VOID(copy_tagged_ptr(src_slot, dst_slot));
        }
        return dst_map;
    }

    logos::expected<void*> copy_object_array(const void* src) noexcept {
        auto* src_arr = static_cast<const ObjectArray*>(src);
        LOGOS_TRY(auto* dst_arr, ObjectArray::create(dst_, src_arr->size()));

        for (uint64_t i = 0; i < src_arr->size(); ++i) {
            LOGOS_TRY_VOID(dst_arr->push_back(AnyVal{}, dst_));
            uint8_t* dst_base = dst_.head().data();
            AnyVal* dst_slot = dst_arr->slot(i, dst_base);

            auto* src_arr_mut = const_cast<ObjectArray*>(src_arr);
            AnyVal* src_slot = src_arr_mut->slot(i, const_cast<uint8_t*>(src_base_));

            LOGOS_TRY_VOID(copy_tagged_ptr(src_slot, dst_slot));
        }
        return dst_arr;
    }

    logos::expected<void*> copy_object_map(const void* src) noexcept {
        auto* src_map = static_cast<const ObjectMap*>(src);
        LOGOS_TRY(auto* dst_map, ObjectMap::create(dst_));

        logos::expected<void> status;
        src_map->for_each([&](ArenaString* src_key, AnyVal* src_val_slot) {
            if (!status) return;
            auto r = dst_map->put(src_key->view(), AnyVal{}, dst_);
            if (!r) { status = std::move(r); return; }
            uint8_t* dst_base = dst_.head().data();
            AnyVal* dst_slot = dst_map->get_slot(src_key->view(), dst_base);
            status = copy_tagged_ptr(src_val_slot, dst_slot);
        }, const_cast<uint8_t*>(src_base_));

        if (!status) return std::unexpected(std::move(status.error()));
        return dst_map;
    }

    logos::expected<void*> copy_fixed(const void* src, TypeTag tag) noexcept {
        size_t size = fixed_type_size(tag.type_code());
        size_t alignment = fixed_type_alignment(tag.type_code());
        LOGOS_TRY(auto* mem_void, dst_.allocate(size, alignment, tag));
        auto* mem = static_cast<uint8_t*>(mem_void);
        std::memcpy(mem, src, size);
        return mem;
    }

    static size_t fixed_type_size(uint64_t tc) {
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

    static size_t fixed_type_alignment(uint64_t tc) {
        switch (tc) {
            case type_hash::TinyInt: case type_hash::UTinyInt: case type_hash::Boolean: return 2;
            case type_hash::SmallInt: case type_hash::USmallInt: return 2;
            case type_hash::Integer: case type_hash::UInteger: case type_hash::Real:
            case type_hash::Time: return 4;
            default: return 8;
        }
    }
};

// --- Free functions ---

logos::expected<HermesCtr> compactify(const HermesCtrView& src) noexcept {
    LOGOS_ASSERT(src.has_root(), "HERMES-DOC-001",
        "Cannot compactify a document without a root object");

    LOGOS_TRY(auto dst, make_doc(HermesCtrAccess::arena(src).total_used() * 2));
    DeepCopyState state(HermesCtrAccess::arena(dst), HermesCtrAccess::base(src));

    arena_offset_t root_off = reinterpret_cast<const DocumentHeader*>(HermesCtrAccess::base(src))->root_offset;
    const void* src_root = HermesCtrAccess::base(src) + root_off.value();
    LOGOS_TRY(auto* dst_root, state.copy_tagged_object(src_root));
    HermesCtrAccess::set_root(dst, dst_root);

    return dst;
}

logos::expected<void*> copy_object_into(const void* src_obj, const uint8_t* src_base,
                                         HermesCtrView& dst) noexcept {
    if (!src_obj) return nullptr;
    DeepCopyState state(HermesCtrAccess::arena(dst), src_base);
    return state.copy_tagged_object(src_obj);
}

logos::expected<HermesCtr> from_bytes_copy(const uint8_t* data, size_t size) noexcept {
    LOGOS_TRY(auto doc, make_doc(size));
    // Copy data into the arena (after the DocumentHeader that make_doc already allocated).
    // Actually, the entire segment IS the data — header is at offset 0.
    // We need to replace the arena content with the provided bytes.
    auto& arena = HermesCtrAccess::arena(doc);
    auto& chunk = arena.head();
    std::memcpy(chunk.data(), data, size);
    chunk.used = size;
    return doc;
}

} // namespace logos::hermes
