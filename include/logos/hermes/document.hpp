// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string_view>

#include <logos/hermes/arena.hpp>
#include <logos/hermes/relative_ptr.hpp>
#include <logos/hermes/tagged_ptr.hpp>
#include <logos/hermes/type_tag.hpp>
#include <logos/hermes/type_registry.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/arena_value.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/object_map.hpp>

// Forward declaration for View layer.
class TinyMapView;
class ArrayView;
class MapView;
class StringView;

namespace logos::hermes {

// DocumentHeader: untagged structure at offset 0 of the arena.
struct DocumentHeader {
    RelativePtr<void> root;
};

static_assert(sizeof(DocumentHeader) == sizeof(arena_offset_t));

// HermesCtr: a Hermes document container. Owns an arena.
// All internal pointers are segment-relative offsets from arena base.
//
// For external (client) use, create via make_shared_ctr() and use View
// methods which return safe handles (View = offset + shared_ptr<HermesCtr>).
//
// For internal (parser/codec) use, create via create() and use raw-pointer
// methods directly.
class HermesCtr : public std::enable_shared_from_this<HermesCtr> {
public:
    // --- Constructors ---

    // Internal: create a value-type document (no shared ownership).
    static HermesCtr create(ArenaMode mode = ArenaMode::MultiChunk, size_t capacity = 65536) {
        (void)mode;
        HermesCtr doc;
        doc.arena_ = std::make_unique<Arena>(ArenaMode::GrowableSingleChunk, capacity);
        auto* hdr = static_cast<DocumentHeader*>(
            doc.arena_->allocate_raw(sizeof(DocumentHeader), alignof(DocumentHeader)));
        hdr->root = RelativePtr<void>{};
        return doc;
    }

    // External: create a shared document that can issue Views.
    static std::shared_ptr<HermesCtr> make_shared_ctr(size_t capacity = 65536) {
        auto doc = std::make_shared<HermesCtr>();
        doc->arena_ = std::make_unique<Arena>(ArenaMode::GrowableSingleChunk, capacity);
        auto* hdr = static_cast<DocumentHeader*>(
            doc->arena_->allocate_raw(sizeof(DocumentHeader), alignof(DocumentHeader)));
        hdr->root = RelativePtr<void>{};
        return doc;
    }

    // --- Segment base ---

    uint8_t* base() { return arena_->head().data(); }
    const uint8_t* base() const { return arena_->head().data(); }

    // --- Root access ---

    DocumentHeader* header() {
        return reinterpret_cast<DocumentHeader*>(base());
    }
    const DocumentHeader* header() const {
        return reinterpret_cast<const DocumentHeader*>(base());
    }

    bool has_root() const { return !header()->root.is_null(); }

    void set_root(void* object) {
        header()->root.set(object, base());
    }

    void set_root_offset(arena_offset_t offset) {
        header()->root.set_offset(offset);
    }

    arena_offset_t offset_of(const void* object) const {
        return static_cast<arena_offset_t>(
            static_cast<const uint8_t*>(object) - base());
    }

    template <typename T>
    T* root() { return static_cast<T*>(header()->root.get(base())); }

    template <typename T>
    const T* root() const { return static_cast<const T*>(header()->root.get(base())); }

    // --- Arena access ---

    Arena& arena() { return *arena_; }
    const Arena& arena() const { return *arena_; }

    // --- Factory helpers ---

    TinyObjectMap* make_tiny_map(uint8_t capacity = 4) {
        return TinyObjectMap::create(*arena_, capacity);
    }

    ObjectArray* make_array(uint64_t capacity = 4) {
        return ObjectArray::create(*arena_, capacity);
    }

    ObjectMap* make_object_map(uint8_t log2_buckets = 3) {
        return ObjectMap::create(*arena_, log2_buckets);
    }

    ArenaString* make_string(std::string_view str) {
        return ArenaString::create(*arena_, str);
    }

    template <typename T>
        requires (TypeTraits<T>::fixed_size && std::is_trivially_copyable_v<T>)
    T* make_value(T value) {
        return arena_put<T>(*arena_, value);
    }

    // --- Compactification ---

    HermesCtr compactify() const;

    // --- Zero-copy serialization ---

    struct ByteSpan {
        const uint8_t* data;
        size_t size;
    };

    ByteSpan as_bytes() const {
        return {arena_->head().data(), arena_->head().used};
    }

    static HermesCtr from_bytes(std::unique_ptr<uint8_t[]> data, size_t size);
    static HermesCtr from_bytes_copy(const uint8_t* data, size_t size);

private:
    std::unique_ptr<Arena> arena_;
};

} // namespace logos::hermes
