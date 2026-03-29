// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <string_view>

#include <logos/hermes/config.hpp>
#include <logos/hermes/mem_holder.hpp>
#include <logos/hermes/own.hpp>
#include <logos/hermes/tagged_ptr.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/object_map.hpp>
#include <logos/hermes/compound_types.hpp>

namespace logos::hermes {

// ---------------------------------------------------------------------------
// ViewBase: common base for all typed views.
//
// Non-owning: stores MemHolder* (raw, no refcount) + arena_offset_t.
// Cheap to create/copy (12 bytes). Use Own<View> for owning semantics.
// ---------------------------------------------------------------------------

class ViewBase {
public:
    ViewBase() noexcept : offset_(NULL_OFFSET), holder_(nullptr) {}
    ViewBase(arena_offset_t offset, MemHolder* holder) noexcept
        : offset_(offset), holder_(holder) {}

    bool is_null() const noexcept { return offset_ == NULL_OFFSET || !holder_; }
    arena_offset_t offset() const noexcept { return offset_; }
    MemHolder* holder() const noexcept { return holder_; }

    void reset() noexcept { offset_ = NULL_OFFSET; holder_ = nullptr; }

protected:
    uint8_t* base() const { return holder_->base(); }
    Arena& arena() const { return holder_->arena(); }

    arena_offset_t offset_;
    MemHolder* holder_;
};

// ---------------------------------------------------------------------------
// Typed Views (non-owning)
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
};

class StringView : public ViewBase {
public:
    using ViewBase::ViewBase;

    ArenaString* ptr() const { return reinterpret_cast<ArenaString*>(base() + offset_); }

    std::string_view view() const { return ptr()->view(); }
    size_t length() const { return ptr()->length(); }

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
};

class ParameterView : public ViewBase {
public:
    using ViewBase::ViewBase;

    ParameterData* ptr() const { return reinterpret_cast<ParameterData*>(base() + offset_); }

    std::string_view name() const { return ptr()->name_view(base()); }
};

// ---------------------------------------------------------------------------
// ObjectView: universal tagged value (non-owning).
// Can hold embedded value (TaggedPtr value mode) or arena pointer.
// ---------------------------------------------------------------------------

class ObjectView {
public:
    ObjectView() noexcept : holder_(nullptr) {}
    ObjectView(TaggedPtr tagged, MemHolder* holder) noexcept
        : tagged_(tagged), holder_(holder) {}

    bool is_null() const noexcept { return tagged_.is_null(); }
    bool is_value() const noexcept { return tagged_.is_value(); }
    bool is_pointer() const noexcept { return tagged_.is_pointer(); }

    template <typename T> T as_value() const { return tagged_.as_value<T>(); }
    uint8_t value_type_hash() const { return tagged_.value_type_hash(); }

    TinyMapView as_tiny_map() const { return {tagged_.to_offset(), holder_}; }
    ArrayView as_array() const { return {tagged_.to_offset(), holder_}; }
    MapView as_map() const { return {tagged_.to_offset(), holder_}; }
    StringView as_string() const { return {tagged_.to_offset(), holder_}; }
    DatatypeView as_datatype() const { return {tagged_.to_offset(), holder_}; }
    ParameterView as_parameter() const { return {tagged_.to_offset(), holder_}; }

    TaggedPtr tagged() const { return tagged_; }
    MemHolder* holder() const noexcept { return holder_; }

    void reset() noexcept { tagged_ = TaggedPtr{}; holder_ = nullptr; }

private:
    TaggedPtr tagged_;
    MemHolder* holder_;
};

// ---------------------------------------------------------------------------
// Owning type aliases — these manage MemHolder refcount.
// Store these. Pass Views for cheap temporary access.
// ---------------------------------------------------------------------------

using TinyMap   = Own<TinyMapView>;
using Array     = Own<ArrayView>;
using Map       = Own<MapView>;
using String    = Own<StringView>;
using Datatype  = Own<DatatypeView>;
using Parameter = Own<ParameterView>;
using Object    = Own<ObjectView>;

// ---------------------------------------------------------------------------
// HermesCtrView: non-owning view of a document.
// ---------------------------------------------------------------------------

class HermesCtrView {
public:
    HermesCtrView() noexcept : holder_(nullptr) {}
    explicit HermesCtrView(MemHolder* holder) noexcept : holder_(holder) {}

    bool is_null() const noexcept { return !holder_; }
    MemHolder* holder() const noexcept { return holder_; }

    void reset() noexcept { holder_ = nullptr; }

    // --- Segment base ---
    uint8_t* base() const { return holder_->base(); }
    Arena& arena() const { return holder_->arena(); }

    // --- Root access ---
    bool has_root() const;
    void set_root(void* object);
    void set_root_offset(arena_offset_t offset);

    template <typename T>
    T* root() const;

    Object root_object() const;

    arena_offset_t offset_of(const void* object) const {
        return static_cast<arena_offset_t>(
            static_cast<const uint8_t*>(object) - base());
    }

    // --- Factory methods returning owning Views ---
    TinyMap make_tiny_map(uint8_t capacity = 4);
    Array make_array(uint64_t capacity = 4);
    Map make_object_map(uint8_t log2_buckets = 3);
    String make_string(std::string_view str);

    // --- Raw-pointer factory methods (for internal parser/codec use) ---
    TinyObjectMap* raw_tiny_map(uint8_t capacity = 4) {
        return TinyObjectMap::create(holder_->arena(), capacity);
    }
    ObjectArray* raw_array(uint64_t capacity = 4) {
        return ObjectArray::create(holder_->arena(), capacity);
    }
    ObjectMap* raw_object_map(uint8_t log2_buckets = 3) {
        return ObjectMap::create(holder_->arena(), log2_buckets);
    }
    ArenaString* raw_string(std::string_view str) {
        return ArenaString::create(holder_->arena(), str);
    }

    template <typename T>
        requires (TypeTraits<T>::fixed_size && std::is_trivially_copyable_v<T>)
    T* make_value(T value) {
        return arena_put<T>(holder_->arena(), value);
    }

private:
    MemHolder* holder_;
};

// HermesCtr: owning document handle.
using HermesCtr = Own<HermesCtrView>;

// Create a new document.
HermesCtr make_doc(size_t capacity = 65536);

} // namespace logos::hermes
