// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <string_view>
#include <format>

#include <logos/writ/mem_holder.hpp>
#include <logos/writ/any_val.hpp>
#include <logos/writ/arena_string.hpp>
#include <logos/writ/object_array.hpp>
#include <logos/writ/tiny_object_map.hpp>
#include <logos/writ/object_map.hpp>
#include <logos/core/expected.hpp>

namespace logos::writ {

// View<Obj> — an OWNING typed view over an arena object. It carries a +1 ref on the
// MemHolder (the residency root) plus the RESOLVED absolute pointer to the object —
// valid because nothing in a Writ segment ever moves while the holder lives.
//
// Owning (not the Writ1 non-owning view + Own<> split): without a borrow checker
// C++ cannot prove the holder outlives the view, so the view must keep it alive.
// Copy → +1 ref, move → transfer, destroy → -1 ref. Navigation (get a child) returns
// another owning view sharing the same holder.
template <typename Obj>
class View {
public:
    View() noexcept = default;

    View(Obj* obj, MemHolder* holder) noexcept : holder_(holder), obj_(obj) {
        if (holder_) holder_->ref();
    }

    // Construct from a within-arena offset + holder (Writ1's `View(offset, holder)`).
    // Resolves obj against the holder's single-chunk base. NULL_OFFSET → null view.
    View(arena_offset_t off, MemHolder* holder) noexcept {
        if (off != NULL_OFFSET && holder) {
            obj_ = reinterpret_cast<Obj*>(holder->base() + off.value());
            holder_ = holder;
            holder_->ref();
        }
    }

    // Construct from a value-form Ref AnyVal (resolves self-relatively). null/Pod → a
    // null view. The cut-over uses this in place of Writ1's `View(av.to_offset(), h)`.
    View(AnyVal av, MemHolder* holder) noexcept {
        if (av.is_ref()) {
            obj_ = reinterpret_cast<Obj*>(const_cast<uint8_t*>(av.resolve()));
            holder_ = holder;
            if (holder_) holder_->ref();
        }
    }

    View(const View& o) noexcept : holder_(o.holder_), obj_(o.obj_) {
        if (holder_) holder_->ref();
    }
    View(View&& o) noexcept : holder_(o.holder_), obj_(o.obj_) {
        o.holder_ = nullptr; o.obj_ = nullptr;
    }
    View& operator=(const View& o) noexcept {
        if (this != &o) {
            if (o.holder_) o.holder_->ref();
            if (holder_) holder_->unref();
            holder_ = o.holder_; obj_ = o.obj_;
        }
        return *this;
    }
    View& operator=(View&& o) noexcept {
        if (this != &o) {
            if (holder_) holder_->unref();
            holder_ = o.holder_; obj_ = o.obj_;
            o.holder_ = nullptr; o.obj_ = nullptr;
        }
        return *this;
    }
    ~View() noexcept { if (holder_) holder_->unref(); }

    bool       is_null() const noexcept { return obj_ == nullptr; }
    explicit operator bool() const noexcept { return obj_ != nullptr; }
    MemHolder* holder() const noexcept { return holder_; }
    Obj*       ptr()    const noexcept { return obj_; }
    // The object's within-arena offset (single-chunk base = holder's head).
    arena_offset_t offset() const noexcept {
        return obj_ ? arena_offset_t(static_cast<uint32_t>(
                          reinterpret_cast<const uint8_t*>(obj_) - holder_->base()))
                    : NULL_OFFSET;
    }

    // The object as a value-form AnyVal Ref (an absolute pointer; re-lowers when
    // stored into a zoned slot).
    AnyVal to_anyval() const noexcept {
        AnyVal a; a.set_ref(obj_); return a;
    }

protected:
    Arena& arena() const noexcept { return holder_->arena(); }

    MemHolder* holder_ = nullptr;
    Obj*       obj_    = nullptr;
};

// ── Typed views ────────────────────────────────────────────────────────────────

class StringView : public View<ArenaString> {
public:
    using View::View;
    std::string_view view() const noexcept { return obj_ ? obj_->view() : std::string_view{}; }
    size_t length() const noexcept { return obj_ ? obj_->length() : 0; }
    size_t size()   const noexcept { return length(); }
    bool   empty()  const noexcept { return length() == 0; }
    // Implicit string_view conversion so `std::string(sv)` and string_view APIs work
    // (the Writ1 readers rely on this).
    operator std::string_view() const noexcept { return view(); }
    std::string to_string() const { return std::string(view()); }
    bool operator==(std::string_view s) const noexcept { return view() == s; }
    bool operator!=(std::string_view s) const noexcept { return view() != s; }
    bool operator==(const StringView& o) const noexcept { return view() == o.view(); }
    bool operator!=(const StringView& o) const noexcept { return view() != o.view(); }
};

class ArrayView : public View<ObjectArray> {
public:
    using View::View;
    uint64_t size() const noexcept { return obj_ ? obj_->size() : 0; }
    bool empty() const noexcept { return size() == 0; }
    AnyVal get(uint64_t i) const noexcept { return obj_ ? obj_->get(i) : AnyVal{}; }
    [[nodiscard]] logos::expected<void> push_back(AnyVal v) noexcept { return obj_->push_back(v, arena()); }
    void set(uint64_t i, AnyVal v) noexcept { if (obj_) obj_->set(i, v); }
};

class TinyMapView : public View<TinyObjectMap> {
public:
    using View::View;
    uint64_t size() const noexcept { return obj_ ? obj_->size() : 0; }
    bool has_key(uint8_t key) const noexcept { return obj_ && obj_->has_key(key); }
    AnyVal get(uint8_t key) const noexcept { return obj_ ? obj_->get(key) : AnyVal{}; }
    [[nodiscard]] logos::expected<void> put(uint8_t key, AnyVal v) noexcept { return obj_->put(key, v, arena()); }
    uint64_t schema_type_code() const noexcept { return obj_ ? obj_->schema_type_code() : 0; }
    void set_schema_type_code(uint64_t c) noexcept { if (obj_) obj_->set_schema_type_code(c); }
    uint64_t bitmap() const noexcept { return obj_ ? obj_->bitmap() : 0; }
    uint64_t capacity() const noexcept { return obj_ ? obj_->capacity() : 0; }
    bool remove(uint8_t key) noexcept { return obj_ ? obj_->remove(key) : false; }
};

class MapView : public View<ObjectMap> {
public:
    using View::View;
    uint64_t size() const noexcept { return obj_ ? obj_->size() : 0; }
    bool has(std::string_view key) const noexcept { return obj_ && obj_->has(key); }
    AnyVal get(std::string_view key) const noexcept { return obj_ ? obj_->get(key) : AnyVal{}; }
    [[nodiscard]] logos::expected<void> put(std::string_view key, AnyVal v) noexcept { return obj_->put(key, v, arena()); }
};

// std::string / string_view on the LEFT vs a StringView on the right (the Writ1
// readers compare both ways).
inline bool operator==(std::string_view a, const StringView& b) noexcept { return a == b.view(); }
inline bool operator!=(std::string_view a, const StringView& b) noexcept { return a != b.view(); }

// ── Navigation: wrap a value-form AnyVal Ref into an owning child view ──────────
// (Sharing the parent's holder — the child lives in the same segment set.)

inline StringView as_string(AnyVal av, MemHolder* h) noexcept {
    return av.is_ref() ? StringView(reinterpret_cast<ArenaString*>(const_cast<uint8_t*>(av.resolve())), h) : StringView{};
}
inline ArrayView as_array(AnyVal av, MemHolder* h) noexcept {
    return av.is_ref() ? ArrayView(reinterpret_cast<ObjectArray*>(const_cast<uint8_t*>(av.resolve())), h) : ArrayView{};
}
inline TinyMapView as_tinymap(AnyVal av, MemHolder* h) noexcept {
    return av.is_ref() ? TinyMapView(reinterpret_cast<TinyObjectMap*>(const_cast<uint8_t*>(av.resolve())), h) : TinyMapView{};
}
inline MapView as_map(AnyVal av, MemHolder* h) noexcept {
    return av.is_ref() ? MapView(reinterpret_cast<ObjectMap*>(const_cast<uint8_t*>(av.resolve())), h) : MapView{};
}

// ── Object — the Writ1 generic node handle, native {AnyVal, holder} ────────────
// A by-value AnyVal (the node's value-form Ref) + the owning holder, with the as_*
// navigation the readers use. Returned by WritCtr::root_object().
class Object {
public:
    Object() noexcept = default;
    Object(AnyVal av, MemHolder* h) noexcept : av_(av), holder_(h) {}

    bool       is_null()  const noexcept { return av_.is_null(); }
    AnyVal     tagged()   const noexcept { return av_; }
    MemHolder* holder()   const noexcept { return holder_; }

    TinyMapView as_tiny_map() const noexcept { return as_tinymap(av_, holder_); }
    ArrayView   as_array()    const noexcept { return logos::writ::as_array(av_, holder_); }
    StringView  as_string()   const noexcept { return logos::writ::as_string(av_, holder_); }
    MapView     as_map()      const noexcept { return logos::writ::as_map(av_, holder_); }

private:
    AnyVal     av_{};
    MemHolder* holder_ = nullptr;
};

} // namespace logos::writ

// Format a StringView like a std::string_view (the readers std::format/println them).
template <>
struct std::formatter<logos::writ::StringView> : std::formatter<std::string_view> {
    auto format(const logos::writ::StringView& s, auto& ctx) const {
        return std::formatter<std::string_view>::format(s.view(), ctx);
    }
};
