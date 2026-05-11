// Logos project — https://github.com/victor-smirnov/logos
//
// HermesAccess — internal accessor for HermesView private members.
//
// This header is intentionally NOT included by document.hpp / view.hpp.
// Only internal Hermes implementation files (parser, codec, path, etc.)
// and low-level tools like the walkthrough should include it.
//
// External code should use the public HermesView API only.

#pragma once

#include <logos/hermes/view.hpp>

namespace logos::hermes {

class HermesAccess {
public:
    static uint8_t* base(const HermesView& v) noexcept  { return v.base(); }
    static uint8_t* base(HermesView& v) noexcept         { return v.base(); }
    static Arena& arena(const HermesView& v) noexcept    { return v.arena(); }
    static Arena& arena(HermesView& v) noexcept           { return v.arena(); }

    template <typename T>
    static T* root(const HermesView& v) noexcept         { return v.root<T>(); }
    template <typename T>
    static T* root(HermesView& v) noexcept               { return v.root<T>(); }

    static arena_offset_t offset_of(const HermesView& v, const void* obj) noexcept {
        return v.offset_of(obj);
    }

    static arena_offset_t root_offset(const HermesView& v) noexcept {
        return reinterpret_cast<const DocumentHeader*>(v.base())->root_offset;
    }

    static void set_root(HermesView& v, void* obj) noexcept { v.set_root(obj); }
    static void set_root_offset(HermesView& v, arena_offset_t off) noexcept { v.set_root_offset(off); }
    static void set_root_override(HermesView& v, arena_offset_t off) noexcept { v.set_root_override(off); }
    static bool has_root_override(const HermesView& v) noexcept { return v.has_root_override(); }

    [[nodiscard]] static logos::expected<TinyObjectMap*> raw_tiny_map(HermesView& v, uint8_t cap = 4) noexcept { return v.raw_tiny_map(cap); }
    [[nodiscard]] static logos::expected<ObjectArray*>   raw_array(HermesView& v, uint64_t cap = 4) noexcept { return v.raw_array(cap); }
    [[nodiscard]] static logos::expected<ObjectMap*>     raw_object_map(HermesView& v, uint8_t log2 = 3) noexcept { return v.raw_object_map(log2); }
    [[nodiscard]] static logos::expected<ArenaString*>   raw_string(HermesView& v, std::string_view s) noexcept { return v.raw_string(s); }

    template <typename T>
        requires (TypeTraits<T>::fixed_size && std::is_trivially_copyable_v<T>)
    [[nodiscard]] static logos::expected<T*> make_value(HermesView& v, T value) noexcept { return v.make_value<T>(value); }
};

} // namespace logos::hermes
