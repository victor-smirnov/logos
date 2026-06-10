// Logos project — https://github.com/victor-smirnov/logos
//
// Hermes2 SPELLING-compat surface for the logosc cut-over (Phase B). Provides the
// Hermes1 NAMES the compiler uses (type code constants, the doc-handle / Object
// spellings, a HermesAccess shim) mapped onto NATIVE hermes2 — NO base/offset model
// is reintroduced (self-relative throughout). Transitional: these spellings can be
// swept to the native tc::/HermesCtr/view forms later. Include this from compiler TUs.

#pragma once

// Umbrella — pulls in the whole hermes2 surface the compiler needs, so every
// `#include <logos/hermes/*.hpp>` in logosc maps to this one header during the cut-over.
#include <logos/hermes2/document.hpp>
#include <logos/hermes2/view.hpp>
#include <logos/hermes2/type_codes.hpp>
#include <logos/hermes2/schema_codes.hpp>
#include <logos/hermes2/any_val.hpp>
#include <logos/hermes2/arena.hpp>
#include <logos/hermes2/type_tag.hpp>
#include <logos/hermes2/config.hpp>
#include <logos/hermes2/mem_holder.hpp>
#include <logos/hermes2/tiny_object_map.hpp>
#include <logos/hermes2/object_array.hpp>
#include <logos/hermes2/object_map.hpp>
#include <logos/hermes2/typed_array.hpp>
#include <logos/hermes2/map.hpp>
#include <logos/hermes2/arena_string.hpp>
#include <logos/hermes2/compound_types.hpp>
#include <logos/hermes2/clone.hpp>
#include <logos/hermes2/binary_codec.hpp>
#include <logos/hermes2/external_ref.hpp>
#include <logos/hermes2/arena_pool.hpp>
#include <logos/hermes2/import_table.hpp>
#include <logos/hermes2/arena_publish.hpp>
#include <logos/hermes2/lir_arena_root.hpp>

namespace logos::hermes2 {

// ── Hermes1 type_hash:: code names → hermes2 tc:: values ────────────────────────
// Structural in-band tags + Pod codes the compiler compares against. NOTE the two
// that CHANGED value: HermesString 28→130 and Bool 37→2 (the hermes2 wire codes).
namespace type_hash {
inline constexpr uint64_t HermesString  = tc::STRING;     // 130 (Hermes1 used 28)
inline constexpr uint64_t Bool          = tc::HA_BOOL;    // 2   (Hermes1 used 37)
inline constexpr uint64_t TinyObjectMap = tc::TINYMAP;    // 98
inline constexpr uint64_t Array         = tc::ARRAY;      // 100
inline constexpr uint64_t Type          = 107;            // schema_type_code value (verbatim)
inline constexpr uint64_t U24           = tc::HT_U24;     // 25
inline constexpr uint64_t MapI32AnyVal  = tc::MAP_I32;
inline constexpr uint64_t MapU32AnyVal  = tc::MAP_U32;
inline constexpr uint64_t MapI64AnyVal  = tc::MAP_I64;
inline constexpr uint64_t MapU64AnyVal  = tc::MAP_U64;
inline constexpr uint64_t ArrayU8       = tc::ARRAY_U8;
inline constexpr uint64_t ArrayU16      = tc::ARRAY_U16;
inline constexpr uint64_t ArrayU32      = tc::ARRAY_U32;
inline constexpr uint64_t ArrayU64      = tc::ARRAY_U64;
inline constexpr uint64_t ArrayI8       = tc::ARRAY_I8;
inline constexpr uint64_t ArrayI16      = tc::ARRAY_I16;
inline constexpr uint64_t ArrayI32      = tc::ARRAY_I32;
inline constexpr uint64_t ArrayI64      = tc::ARRAY_I64;
inline constexpr uint64_t ArrayF32      = tc::ARRAY_F32;
inline constexpr uint64_t ArrayF64      = tc::ARRAY_F64;
}  // namespace type_hash

// ── Object — the Hermes1 generic node handle, native form {AnyVal, holder} ───────
// A by-value AnyVal (the node's value-form Ref) plus the owning holder, with the
// as_* navigation the readers use. Returned by HermesCtr::root_object().
class Object {
public:
    Object() noexcept = default;
    Object(AnyVal av, MemHolder* h) noexcept : av_(av), holder_(h) {}

    bool       is_null()  const noexcept { return av_.is_null(); }
    AnyVal     tagged()   const noexcept { return av_; }
    MemHolder* holder()   const noexcept { return holder_; }

    TinyMapView as_tiny_map() const noexcept { return as_tinymap(av_, holder_); }
    ArrayView   as_array()    const noexcept { return logos::hermes2::as_array(av_, holder_); }
    StringView  as_string()   const noexcept { return logos::hermes2::as_string(av_, holder_); }
    MapView     as_map()      const noexcept { return logos::hermes2::as_map(av_, holder_); }

private:
    AnyVal     av_{};
    MemHolder* holder_ = nullptr;
};

// ── Doc-handle spellings ─────────────────────────────────────────────────────────
using HermesView = HermesCtr;
using Hermes     = HermesCtr;

// root_object() on the doc handle (Hermes1 spelling).
inline Object root_object(const HermesCtr& d) noexcept { return Object(d.root(), d.holder()); }

// Load a document from a compacted blob (Hermes1 spelling of HermesCtr::from_bytes).
[[nodiscard]] inline logos::expected<HermesCtr>
from_bytes_copy(const uint8_t* data, size_t size) noexcept {
    return HermesCtr::from_bytes(data, size);
}

// ── HermesAccess shim — native (no base/offset model) ───────────────────────────
// raw_* + arena map to HermesCtr methods. base() is VESTIGIAL (the head chunk start)
// and is only ever fed to AnyVal::as_ptr(base)/set_pointer(target,base) call sites,
// which IGNORE it (self-relative). Sweep those base args away in the cut-over.
class HermesAccess {
public:
    static Arena& arena(HermesCtr& d) noexcept { return d.arena(); }
    static Arena& arena(const HermesCtr& d) noexcept { return const_cast<HermesCtr&>(d).arena(); }
    static uint8_t* base(const HermesCtr& d) noexcept { return d.holder()->arena().head().data(); }

    [[nodiscard]] static logos::expected<TinyObjectMap*> raw_tiny_map(HermesCtr& d, uint64_t cap = 4) noexcept { return d.make_tiny_map(cap); }
    [[nodiscard]] static logos::expected<ArrayView>      raw_array(HermesCtr& d, uint64_t cap = 4) noexcept { return d.make_array(cap); }
    [[nodiscard]] static logos::expected<MapView>        raw_object_map(HermesCtr& d, uint64_t cap = 8) noexcept { return d.make_object_map(cap); }
    [[nodiscard]] static logos::expected<StringView>     raw_string(HermesCtr& d, std::string_view s) noexcept { return d.make_string(s); }
};

}  // namespace logos::hermes2
