// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// HermesCtrAccess — internal accessor for HermesCtrView private members.
//
// This header is intentionally NOT included by document.hpp / view.hpp.
// Only internal Hermes implementation files (parser, codec, path, etc.)
// and low-level tools like the walkthrough should include it.
//
// External code should use the public HermesCtrView API only.

#pragma once

#include <logos/hermes/view.hpp>

namespace logos::hermes {

class HermesCtrAccess {
public:
    static uint8_t* base(const HermesCtrView& v) noexcept  { return v.base(); }
    static uint8_t* base(HermesCtrView& v) noexcept         { return v.base(); }
    static Arena& arena(const HermesCtrView& v) noexcept    { return v.arena(); }
    static Arena& arena(HermesCtrView& v) noexcept           { return v.arena(); }

    template <typename T>
    static T* root(const HermesCtrView& v) noexcept         { return v.root<T>(); }
    template <typename T>
    static T* root(HermesCtrView& v) noexcept               { return v.root<T>(); }

    static arena_offset_t offset_of(const HermesCtrView& v, const void* obj) noexcept {
        return v.offset_of(obj);
    }

    static void set_root(HermesCtrView& v, void* obj) noexcept { v.set_root(obj); }
    static void set_root_offset(HermesCtrView& v, arena_offset_t off) noexcept { v.set_root_offset(off); }
    static void set_root_override(HermesCtrView& v, arena_offset_t off) noexcept { v.set_root_override(off); }
    static bool has_root_override(const HermesCtrView& v) noexcept { return v.has_root_override(); }

    [[nodiscard]] static logos::expected<TinyObjectMap*> raw_tiny_map(HermesCtrView& v, uint8_t cap = 4) noexcept { return v.raw_tiny_map(cap); }
    [[nodiscard]] static logos::expected<ObjectArray*>   raw_array(HermesCtrView& v, uint64_t cap = 4) noexcept { return v.raw_array(cap); }
    [[nodiscard]] static logos::expected<ObjectMap*>     raw_object_map(HermesCtrView& v, uint8_t log2 = 3) noexcept { return v.raw_object_map(log2); }
    [[nodiscard]] static logos::expected<ArenaString*>   raw_string(HermesCtrView& v, std::string_view s) noexcept { return v.raw_string(s); }

    template <typename T>
        requires (TypeTraits<T>::fixed_size && std::is_trivially_copyable_v<T>)
    [[nodiscard]] static logos::expected<T*> make_value(HermesCtrView& v, T value) noexcept { return v.make_value<T>(value); }
};

} // namespace logos::hermes
