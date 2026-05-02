// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <format>
#include <string_view>

#include <logos/hermes/config.hpp>
#include <logos/hermes/mem_holder.hpp>
#include <logos/hermes/own.hpp>
#include <logos/hermes/any_val.hpp>
#include <logos/hermes/named_code.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/type_registry.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/object_map.hpp>
#include <logos/hermes/compound_types.hpp>

namespace logos::hermes {

// Forward declaration — ObjectView is defined below, after the typed views.
class ObjectView;

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

    // Convert this view to an AnyVal pointer (for storing in a map/array slot).
    AnyVal to_anyval() const noexcept { return AnyVal::from_offset(offset_); }

    void reset() noexcept { offset_ = NULL_OFFSET; holder_ = nullptr; }

protected:
    uint8_t* base() const noexcept { return holder_->base(); }
    Arena& arena() const noexcept { return holder_->arena(); }

    arena_offset_t offset_;
    MemHolder* holder_;
};

// ---------------------------------------------------------------------------
// Typed Views (non-owning)
// ---------------------------------------------------------------------------

class TinyMapView : public ViewBase {
public:
    using ViewBase::ViewBase;

    TinyObjectMap* ptr() const noexcept { return reinterpret_cast<TinyObjectMap*>(base() + offset_.value()); }

    uint8_t size() const noexcept { return ptr()->size(); }
    uint64_t bitmap() const noexcept { return ptr()->bitmap(); }
    bool has_key(uint8_t key) const noexcept { return ptr()->has_key(key); }

    AnyVal get(uint8_t key) const noexcept { return ptr()->get(key, base()); }
    AnyVal* slot(uint8_t key) const noexcept { return ptr()->slot(key, base()); }

    // Checked access: asserts the key exists and includes the field name in the error.
    AnyVal get(NamedCode<uint8_t> key) const;

    bool has_key(NamedCode<uint8_t> key) const noexcept { return has_key(key.code); }

    [[nodiscard]] logos::expected<void> put(uint8_t key, AnyVal value) noexcept {
        return ptr()->put(key, value, arena());
    }
    [[nodiscard]] logos::expected<void> put(NamedCode<uint8_t> key, AnyVal value) noexcept {
        return put(key.code, value);
    }

    // Cross-arena safe: deep-copies value into this arena if it comes from a different one.
    [[nodiscard]] logos::expected<void> put(uint8_t key, const ObjectView& value) noexcept;
    [[nodiscard]] logos::expected<void> put(NamedCode<uint8_t> key, const ObjectView& value) noexcept {
        return put(key.code, value);
    }
};

class ArrayView : public ViewBase {
public:
    using ViewBase::ViewBase;

    ObjectArray* ptr() const noexcept { return reinterpret_cast<ObjectArray*>(base() + offset_.value()); }

    uint64_t size() const noexcept { return ptr()->size(); }
    bool empty() const noexcept { return ptr()->empty(); }

    AnyVal get(uint64_t index) const noexcept { return ptr()->get(index, base()); }
    AnyVal* slot(uint64_t index) const noexcept { return ptr()->slot(index, base()); }

    [[nodiscard]] logos::expected<void> push_back(AnyVal value) noexcept { return ptr()->push_back(value, arena()); }

    // Cross-arena safe: deep-copies value into this arena if it comes from a different one.
    [[nodiscard]] logos::expected<void> push_back(const ObjectView& value) noexcept;
};

class MapView : public ViewBase {
public:
    using ViewBase::ViewBase;

    ObjectMap* ptr() const noexcept { return reinterpret_cast<ObjectMap*>(base() + offset_.value()); }

    uint64_t size() const noexcept { return ptr()->size(); }
    bool empty() const noexcept { return ptr()->empty(); }

    AnyVal get(std::string_view key) const noexcept { return ptr()->get(key, base()); }
    AnyVal* get_slot(std::string_view key) const noexcept { return ptr()->get_slot(key, base()); }
    bool has(std::string_view key) const noexcept { return ptr()->has(key, base()); }

    [[nodiscard]] logos::expected<void> put(std::string_view key, AnyVal value) noexcept { return ptr()->put(key, value, arena()); }

    // Cross-arena safe: deep-copies value into this arena if it comes from a different one.
    [[nodiscard]] logos::expected<void> put(std::string_view key, const ObjectView& value) noexcept;

    template <typename Fn>
    void for_each(Fn fn) const noexcept { ptr()->for_each(fn, base()); }
};

class StringView : public ViewBase {
public:
    using ViewBase::ViewBase;

    ArenaString* ptr() const noexcept { return reinterpret_cast<ArenaString*>(base() + offset_.value()); }

    // view() is the unsafe escape hatch — its result is freshly computed each
    // call but, once obtained, becomes stale at the next arena allocation.
    // Use it only within a single expression. For comparisons, `operator==`
    // and `to_string()` below are realloc-safe (each refetches via base()).
    std::string_view view() const noexcept { return ptr()->view(); }
    size_t length() const noexcept { return ptr()->length(); }
    size_t size()   const noexcept { return ptr()->length(); }
    bool   empty()  const noexcept { return is_null() || ptr()->length() == 0; }

    bool operator==(std::string_view other) const noexcept {
        return std::string_view{is_null() ? std::string_view{} : view()} == other;
    }
    bool operator!=(std::string_view other) const noexcept { return !(*this == other); }
    bool operator==(const StringView& other) const noexcept {
        return std::string_view{*this} == std::string_view{other};
    }
    bool operator!=(const StringView& other) const noexcept { return !(*this == other); }

    // Owning copy into a heap std::string. Realloc-safe (single read).
    std::string to_string() const { return is_null() ? std::string{} : std::string(view()); }

    // Explicit conversion to std::string copies out (realloc-safe). Explicit
    // so it does not interfere with operator==(string_view) overload resolution
    // when comparing against string literals; `std::string(sv)` still works.
    explicit operator std::string() const { return to_string(); }

    // Implicit conversion to std::string_view: each call refetches via base(),
    // so the resulting string_view is realloc-safe at the moment of the call,
    // but stale at the next arena allocation. Use only within a single
    // expression. Implicit so existing string_view-taking APIs (sema_key,
    // string::append, etc.) accept OStringView without rewrites.
    operator std::string_view() const noexcept { return is_null() ? std::string_view{} : view(); }
};

// Free comparison operators in hermes namespace so ADL finds them when the
// LHS is std::string_view (or std::string, which converts implicitly to
// string_view). Member operator== covers the StringView-on-LHS case.
// LHS-is-string overloads. Needed because the member operator== reverse-
// rewriting does not cross the std::string→std::string_view standard
// conversion through user-defined-conversion candidates cleanly.
inline bool operator==(std::string_view lhs, const StringView& rhs) noexcept { return rhs == lhs; }
inline bool operator!=(std::string_view lhs, const StringView& rhs) noexcept { return rhs != lhs; }

class DatatypeView : public ViewBase {
public:
    using ViewBase::ViewBase;

    DatatypeData* ptr() const noexcept { return reinterpret_cast<DatatypeData*>(base() + offset_.value()); }

    std::string_view name() const noexcept { return ptr()->name_view(base()); }
    bool has_params() const noexcept { return ptr()->has_params(); }
    bool has_ctr() const noexcept { return ptr()->has_ctr(); }
};

class ParameterView : public ViewBase {
public:
    using ViewBase::ViewBase;

    ParameterData* ptr() const noexcept { return reinterpret_cast<ParameterData*>(base() + offset_.value()); }

    std::string_view name() const noexcept { return ptr()->name_view(base()); }
};

// Component-metaprog slice 1E: non-owning view over a Logos-Type Hermes value.
// A Logos Type is a TinyObjectMap whose schema_type_code = type_hash::Type=107
// carrying:
//   key 0 → kind  (u32, inline AnyVal)
//   key 1 → uid   (u64, ptr-mode AnyVal)
//   key 2 → name  (ArenaString, ptr-mode AnyVal)
class TypeView : public ViewBase {
public:
    using ViewBase::ViewBase;

    TinyObjectMap* ptr() const noexcept {
        return reinterpret_cast<TinyObjectMap*>(base() + offset_.value());
    }

    bool valid() const noexcept {
        return !is_null() && ptr()->schema_type_code() == type_hash::Type;
    }

    uint32_t kind() const noexcept {
        auto av = ptr()->get(0, base());
        return av.is_null() ? 0u : av.as_value<uint32_t>();
    }

    uint64_t uid() const noexcept {
        auto av = ptr()->get(1, base());
        if (av.is_null()) return 0;
        return *av.as_ptr<const uint64_t>(base());
    }

    std::string_view name() const noexcept {
        auto av = ptr()->get(2, base());
        if (av.is_null()) return {};
        return av.as_ptr<const ArenaString>(base())->view();
    }
};

// ---------------------------------------------------------------------------
// ObjectView: universal tagged value (non-owning).
// Can hold embedded value (AnyVal value mode) or arena pointer.
// ---------------------------------------------------------------------------

class ObjectView {
public:
    ObjectView() noexcept : holder_(nullptr) {}
    ObjectView(AnyVal tagged, MemHolder* holder) noexcept
        : tagged_(tagged), holder_(holder) {}

    bool is_null() const noexcept { return tagged_.is_null(); }
    bool is_value() const noexcept { return tagged_.is_value(); }
    bool is_pointer() const noexcept { return tagged_.is_pointer(); }

    template <typename T> T as_value() const noexcept { return tagged_.as_value<T>(); }
    uint8_t value_type_hash() const noexcept { return tagged_.value_type_hash(); }

    TinyMapView as_tiny_map() const noexcept { return {tagged_.to_offset(), holder_}; }
    ArrayView as_array() const noexcept { return {tagged_.to_offset(), holder_}; }
    MapView as_map() const noexcept { return {tagged_.to_offset(), holder_}; }
    StringView as_string() const noexcept { return {tagged_.to_offset(), holder_}; }
    DatatypeView as_datatype() const noexcept { return {tagged_.to_offset(), holder_}; }
    ParameterView as_parameter() const noexcept { return {tagged_.to_offset(), holder_}; }
    TypeView as_type() const noexcept { return {tagged_.to_offset(), holder_}; }

    AnyVal tagged() const noexcept { return tagged_; }
    MemHolder* holder() const noexcept { return holder_; }

    void reset() noexcept { tagged_ = AnyVal{}; holder_ = nullptr; }

private:
    AnyVal tagged_;
    MemHolder* holder_;
};

// ---------------------------------------------------------------------------
// Owning type aliases — these manage MemHolder refcount.
// Store these. Pass Views for cheap temporary access.
// ---------------------------------------------------------------------------

using TinyMap     = Own<TinyMapView>;
using Array       = Own<ArrayView>;
using Map         = Own<MapView>;
using String      = Own<StringView>;
using OStringView = Own<StringView>;  // Canonical name in compiler / fact-base code.
using Datatype    = Own<DatatypeView>;
using Parameter   = Own<ParameterView>;
using Object      = Own<ObjectView>;

// ---------------------------------------------------------------------------
// HermesView: non-owning view of a document.
// ---------------------------------------------------------------------------

class HermesAccess;

class HermesView {
public:
    HermesView() noexcept : holder_(nullptr), root_override_(NULL_OFFSET) {}
    explicit HermesView(MemHolder* holder) noexcept
        : holder_(holder), root_override_(NULL_OFFSET) {}

    bool is_null() const noexcept { return !holder_; }
    MemHolder* holder() const noexcept { return holder_; }

    void reset() noexcept { holder_ = nullptr; root_override_ = NULL_OFFSET; }

    // --- Seal (immutable sharing across reactors) ---

    // Seal the underlying arena: no further allocations allowed.
    // After sealing, this document (and any copies via Own<>) can be safely
    // read from multiple reactors concurrently — the arena content is immutable.
    void seal() noexcept { holder_->arena().seal(); }

    // True if the arena has been sealed.
    bool is_sealed() const noexcept { return holder_->arena().is_sealed(); }

    // --- Root access ---
    bool has_root() const noexcept;

    // Set root from any View (TinyMap, Array, Map, String, etc.) — public API.
    void set_root(const ViewBase& view) noexcept { set_root_offset(view.offset()); }

    Object root_object() const noexcept;

    // --- Factory methods returning owning Views ---
    [[nodiscard]] logos::expected<TinyMap> make_tiny_map(uint8_t capacity = 4) noexcept;
    [[nodiscard]] logos::expected<Array> make_array(uint64_t capacity = 4) noexcept;
    [[nodiscard]] logos::expected<Map> make_object_map(uint8_t log2_buckets = 3) noexcept;
    [[nodiscard]] logos::expected<String> make_string(std::string_view str) noexcept;

    // Arena checkpoint / rollback (GrowableSingleChunk only).
    // Used by generated PEG parsers to reclaim arena memory on backtrack.
    size_t arena_checkpoint() const noexcept { return arena().checkpoint(); }
    void   arena_rollback(size_t pos)  noexcept { arena().rollback(pos); }

private:
    friend class HermesAccess;

    // --- Segment base ---
    uint8_t* base() const noexcept { return holder_->base(); }
    Arena& arena() const noexcept { return holder_->arena(); }

    // --- Internal root manipulation (used by parser, codec, path evaluator) ---
    void set_root(void* object) noexcept;
    void set_root_offset(arena_offset_t offset) noexcept;
    void set_root_override(arena_offset_t offset) noexcept { root_override_ = offset; }
    bool has_root_override() const noexcept { return root_override_ != NULL_OFFSET; }

    template <typename T>
    T* root() const noexcept;

    arena_offset_t offset_of(const void* object) const noexcept {
        return arena_offset_t{static_cast<arena_offset_t::value_type>(
            static_cast<const uint8_t*>(object) - base())};
    }

    // --- Raw-pointer factory methods (for internal parser/codec use) ---
    [[nodiscard]] logos::expected<TinyObjectMap*> raw_tiny_map(uint8_t capacity = 4) noexcept {
        return TinyObjectMap::create(holder_->arena(), capacity);
    }
    [[nodiscard]] logos::expected<ObjectArray*> raw_array(uint64_t capacity = 4) noexcept {
        return ObjectArray::create(holder_->arena(), capacity);
    }
    [[nodiscard]] logos::expected<ObjectMap*> raw_object_map(uint8_t log2_buckets = 3) noexcept {
        return ObjectMap::create(holder_->arena(), log2_buckets);
    }
    [[nodiscard]] logos::expected<ArenaString*> raw_string(std::string_view str) noexcept {
        return ArenaString::create(holder_->arena(), str);
    }

    template <typename T>
        requires (TypeTraits<T>::fixed_size && std::is_trivially_copyable_v<T>)
    [[nodiscard]] logos::expected<T*> make_value(T value) noexcept {
        LOGOS_TRY(auto* p, arena_put<T>(holder_->arena(), value));
        return p;
    }

    MemHolder* holder_;
    arena_offset_t root_override_;
};

// Hermes: owning document handle.
using Hermes = Own<HermesView>;

// Create a new document (GrowableSingleChunk — base is stable within size, moves on grow).
[[nodiscard]] logos::expected<Hermes> make_doc(size_t capacity = 65536) noexcept;

// Create a new document with MultiChunk arena (base is always stable).
[[nodiscard]] logos::expected<Hermes> make_doc_multi(size_t initial_capacity = 4096) noexcept;

// Deep-copy a single tagged object from src_base arena into dst document's arena.
// Returns pointer into dst's arena. Useful for cross-arena pointer resolution.
logos::expected<void*> copy_object_into(const void* src_obj, const uint8_t* src_base,
                                         HermesView& dst) noexcept;

// Load a document from raw bytes (copies the data).
[[nodiscard]] logos::expected<Hermes> from_bytes_copy(const uint8_t* data, size_t size) noexcept;

} // namespace logos::hermes

// std::formatter specialization for StringView (and Own<StringView> via
// inheritance). Delegates to the std::string_view formatter — refetches the
// view at format time, so it is realloc-safe at the moment of formatting.
template <>
struct std::formatter<logos::hermes::StringView, char>
    : std::formatter<std::string_view, char>
{
    template <typename FormatContext>
    auto format(const logos::hermes::StringView& sv, FormatContext& ctx) const {
        return std::formatter<std::string_view, char>::format(
            sv.is_null() ? std::string_view{} : sv.view(), ctx);
    }
};

template <>
struct std::formatter<logos::hermes::Own<logos::hermes::StringView>, char>
    : std::formatter<logos::hermes::StringView, char>
{};
