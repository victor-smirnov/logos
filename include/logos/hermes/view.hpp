// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <memory>
#include <string_view>

#include <logos/hermes/config.hpp>
#include <logos/hermes/tagged_ptr.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/object_map.hpp>
#include <logos/hermes/compound_types.hpp>

namespace logos::hermes {

class HermesCtr; // Forward declaration.

// MemRef: shared reference to a HermesCtr. Keeps the document (and its arena)
// alive while any View referencing it exists. Lightweight (shared_ptr copy).
using MemRef = std::shared_ptr<HermesCtr>;

// ---------------------------------------------------------------------------
// ViewBase: common base for all typed views.
//
// Stores:
//   - arena_offset_t offset_  (stable across realloc)
//   - MemRef          mem_    (keeps document alive, provides fresh base())
//
// Dereference: mem_->base() + offset_ → raw pointer to the arena object.
// This is always valid because:
//   1. MemRef keeps HermesCtr alive (shared ownership)
//   2. offset_ is segment-relative (stable across realloc)
//   3. base() returns the current arena base (updated after realloc)
// ---------------------------------------------------------------------------

class ViewBase {
public:
    ViewBase() : offset_(NULL_OFFSET) {}
    ViewBase(arena_offset_t offset, MemRef mem) : offset_(offset), mem_(std::move(mem)) {}

    bool is_null() const { return offset_ == NULL_OFFSET || !mem_; }
    arena_offset_t offset() const { return offset_; }
    const MemRef& mem() const { return mem_; }

protected:
    // Get the current segment base. Always fresh — survives realloc.
    uint8_t* base() const;

    arena_offset_t offset_;
    MemRef mem_;
};

// ---------------------------------------------------------------------------
// Typed Views
// ---------------------------------------------------------------------------

class TinyMapView : public ViewBase {
public:
    using ViewBase::ViewBase;

    TinyObjectMap* ptr() const { return reinterpret_cast<TinyObjectMap*>(base() + offset_); }

    uint8_t size() const { return ptr()->size(); }
    uint64_t bitmap() const { return ptr()->bitmap(); }
    bool has_key(uint8_t key) const { return ptr()->has_key(key); }

    TaggedPtr get(uint8_t key) const { return ptr()->get(key, base()); }
    TaggedPtr* slot(uint8_t key) const { return ptr()->slot(key, base()); }

    void put(uint8_t key, TaggedPtr value) { ptr()->put(key, value, arena()); }

private:
    Arena& arena() const;
};

class ArrayView : public ViewBase {
public:
    using ViewBase::ViewBase;

    ObjectArray* ptr() const { return reinterpret_cast<ObjectArray*>(base() + offset_); }

    uint64_t size() const { return ptr()->size(); }
    bool empty() const { return ptr()->empty(); }

    TaggedPtr get(uint64_t index) const { return ptr()->get(index, base()); }
    TaggedPtr* slot(uint64_t index) const { return ptr()->slot(index, base()); }

    void push_back(TaggedPtr value) { ptr()->push_back(value, arena()); }

private:
    Arena& arena() const;
};

class MapView : public ViewBase {
public:
    using ViewBase::ViewBase;

    ObjectMap* ptr() const { return reinterpret_cast<ObjectMap*>(base() + offset_); }

    uint64_t size() const { return ptr()->size(); }
    bool empty() const { return ptr()->empty(); }

    TaggedPtr get(std::string_view key) const { return ptr()->get(key, base()); }
    TaggedPtr* get_slot(std::string_view key) const { return ptr()->get_slot(key, base()); }
    bool has(std::string_view key) const { return ptr()->has(key, base()); }

    void put(std::string_view key, TaggedPtr value) { ptr()->put(key, value, arena()); }

    template <typename Fn>
    void for_each(Fn fn) const { ptr()->for_each(fn, base()); }

private:
    Arena& arena() const;
};

class StringView : public ViewBase {
public:
    using ViewBase::ViewBase;

    ArenaString* ptr() const { return reinterpret_cast<ArenaString*>(base() + offset_); }

    std::string_view view() const { return ptr()->view(); }
    size_t length() const { return ptr()->length(); }
    uint64_t hash() const { return ptr()->hash(); }

    bool operator==(std::string_view other) const { return view() == other; }
    bool operator!=(std::string_view other) const { return view() != other; }
};

class DatatypeView : public ViewBase {
public:
    using ViewBase::ViewBase;

    DatatypeData* ptr() const { return reinterpret_cast<DatatypeData*>(base() + offset_); }

    std::string_view name() const { return ptr()->name_view(base()); }
    bool has_params() const { return ptr()->has_params(); }
    bool has_ctr() const { return ptr()->has_ctr(); }
    bool is_const() const { return ptr()->is_const(); }
    bool is_volatile() const { return ptr()->is_volatile(); }
};

class ParameterView : public ViewBase {
public:
    using ViewBase::ViewBase;

    ParameterData* ptr() const { return reinterpret_cast<ParameterData*>(base() + offset_); }

    std::string_view name() const { return ptr()->name_view(base()); }
};

// ---------------------------------------------------------------------------
// ObjectView: universal tagged value — can hold any Hermes value.
// Wraps a TaggedPtr + MemRef. The TaggedPtr may be embedded (value mode)
// or point to an arena object (pointer mode).
// ---------------------------------------------------------------------------

class ObjectView {
public:
    ObjectView() : tagged_{} {}
    ObjectView(TaggedPtr tagged, MemRef mem) : tagged_(tagged), mem_(std::move(mem)) {}

    bool is_null() const { return tagged_.is_null(); }
    bool is_value() const { return tagged_.is_value(); }
    bool is_pointer() const { return tagged_.is_pointer(); }

    // For embedded values:
    template <typename T> T as_value() const { return tagged_.as_value<T>(); }
    uint8_t value_type_hash() const { return tagged_.value_type_hash(); }

    // For pointer-mode values — resolve to typed view:
    TinyMapView as_tiny_map() const;
    ArrayView as_array() const;
    MapView as_map() const;
    StringView as_string() const;
    DatatypeView as_datatype() const;
    ParameterView as_parameter() const;

    // Raw TaggedPtr access.
    TaggedPtr tagged() const { return tagged_; }
    const MemRef& mem() const { return mem_; }

private:
    TaggedPtr tagged_;
    MemRef mem_;
};

// ---------------------------------------------------------------------------
// Document: a shared HermesCtr wrapper that produces Views.
// This is the primary API for external (client) code.
// ---------------------------------------------------------------------------

class Document {
public:
    Document() = default;

    static Document create(size_t capacity = 65536);

    bool is_null() const { return !ctr_; }
    bool has_root() const;

    void set_root(const ViewBase& view);
    void set_root_offset(arena_offset_t offset);

    ObjectView root() const;

    TinyMapView make_tiny_map(uint8_t capacity = 4);
    ArrayView make_array(uint64_t capacity = 4);
    MapView make_object_map(uint8_t log2_buckets = 3);
    StringView make_string(std::string_view str);

    HermesCtr& ctr();
    const HermesCtr& ctr() const;
    MemRef mem_ref() const { return ctr_; }

private:
    std::shared_ptr<HermesCtr> ctr_;
    Document(std::shared_ptr<HermesCtr> ctr) : ctr_(std::move(ctr)) {}
};

} // namespace logos::hermes
