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

namespace logos::hermes {

// DocumentHeader: untagged structure at offset 0 of the first arena chunk.
// Contains a single relative pointer to the root object.
struct DocumentHeader {
    RelativePtr<void> root;
};

static_assert(sizeof(DocumentHeader) == 8);

// HermesCtr: a Hermes document container. Owns an arena and provides
// the high-level API for creating and manipulating Hermes documents.
//
// Usage:
//   auto doc = HermesCtr::create();
//   auto* map = doc.make_tiny_map();
//   doc.set_root(map);
//   map->put(0, TaggedPtr::from_value(int32_t(42), type_hash::Integer), doc.arena());
//
//   auto compact = doc.compactify();
//   auto bytes = compact.as_bytes();  // zero-copy serialization
class HermesCtr {
public:
    // Create a new empty document.
    static HermesCtr create(ArenaMode mode = ArenaMode::MultiChunk, size_t capacity = 4096) {
        HermesCtr doc;
        doc.arena_ = std::make_unique<Arena>(mode, capacity);
        // Allocate the document header at offset 0.
        auto* hdr = static_cast<DocumentHeader*>(
            doc.arena_->allocate_raw(sizeof(DocumentHeader), alignof(DocumentHeader)));
        hdr->root = RelativePtr<void>{};  // null root
        return doc;
    }

    // --- Root access ---

    DocumentHeader* header() {
        return reinterpret_cast<DocumentHeader*>(arena_->head().data());
    }

    const DocumentHeader* header() const {
        return reinterpret_cast<const DocumentHeader*>(arena_->head().data());
    }

    bool has_root() const { return !header()->root.is_null(); }

    // Set the root object. The object must be allocated in this document's arena.
    void set_root(void* object) { header()->root.set(object); }

    template <typename T>
    T* root() { return static_cast<T*>(header()->root.get()); }

    template <typename T>
    const T* root() const { return static_cast<const T*>(header()->root.get()); }

    // --- Arena access ---

    Arena& arena() { return *arena_; }
    const Arena& arena() const { return *arena_; }

    // --- Factory helpers (allocate objects in this document's arena) ---

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

    // Create a new document with a single contiguous chunk containing a deep copy
    // of all reachable objects from the root. The result is suitable for zero-copy
    // serialization.
    HermesCtr compactify() const;

    // --- Zero-copy serialization ---

    // Get the raw bytes of a compacted (single-chunk) document.
    // Only valid if the document is GrowableSingleChunk mode.
    struct ByteSpan {
        const uint8_t* data;
        size_t size;
    };

    ByteSpan as_bytes() const {
        return {arena_->head().data(), arena_->head().used};
    }

    // Load a document from raw bytes (zero-copy — takes ownership of the buffer).
    static HermesCtr from_bytes(std::unique_ptr<uint8_t[]> data, size_t size);

    // Load a document from raw bytes (copies the data).
    static HermesCtr from_bytes_copy(const uint8_t* data, size_t size);

private:
    std::unique_ptr<Arena> arena_;
};

} // namespace logos::hermes
