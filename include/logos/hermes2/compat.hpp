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

// Hermes1's type-ops registry init — hermes2 has no per-type ops vtable (clone/
// stringify dispatch directly on the TypeTag), so this is a no-op.
inline void hermes_init() noexcept {}

// ── Doc-handle spellings ─────────────────────────────────────────────────────────
// (Object lives in view.hpp; HermesCtr::root_object() returns it.)
using HermesView = HermesCtr;
using Hermes     = HermesCtr;

// Load a document from a compacted blob (Hermes1 spelling of HermesCtr::from_bytes).
[[nodiscard]] inline logos::expected<HermesCtr>
from_bytes_copy(const uint8_t* data, size_t size) noexcept {
    return HermesCtr::from_bytes(data, size);
}

// Deep-copy a tagged object from another document into `dst` (the metaprog blob splice).
// `src_base` is vestigial (hermes2 is self-relative; src_obj is an absolute pointer the
// deep-copy walks via resolve()). Returns the dst object pointer.
[[nodiscard]] inline logos::expected<void*>
copy_object_into(const void* src_obj, const uint8_t* /*src_base*/, HermesCtr& dst) noexcept {
    DeepCopyState st(dst.holder());
    void* d = deep_copy_object(reinterpret_cast<const uint8_t*>(src_obj), st);
    if (!d) return std::unexpected(logos::err(ErrCode::out_of_memory));
    return d;
}

// Box a wide scalar (i64/u64/f32/f64 — doesn't fit the inline Pod) into the arena and
// return a Ref AnyVal to it (Hermes1 anyval_put / arena_put). Tag = the matching tc code.
template <typename T>
[[nodiscard]] inline logos::expected<AnyVal> anyval_put(Arena& arena, T v) noexcept {
    uint64_t code;
    if constexpr (std::is_same_v<T, double>)        code = tc::F64;
    else if constexpr (std::is_same_v<T, float>)    code = tc::F32;
    else if constexpr (std::is_unsigned_v<T>)       code = tc::U64;
    else                                            code = tc::I64;
    LOGOS_TRY(void* mem, arena.allocate(sizeof(T), alignof(T) < 2 ? 2 : alignof(T), TypeTag(code)));
    std::memcpy(mem, &v, sizeof(T));
    AnyVal a; a.set_ref(mem); return a;
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

    // Root offset accessors (the mirror/TypePool address by offset within their single
    // chunk). root() returns the root AnyVal; its offset is relative to base().
    static arena_offset_t root_offset(const HermesCtr& d) noexcept {
        AnyVal r = d.root();
        return r.is_ref() ? r.to_offset(base(d)) : NULL_OFFSET;
    }
    static void set_root_offset(HermesCtr& d, arena_offset_t off) noexcept {
        d.set_root(off == NULL_OFFSET ? AnyVal{} : AnyVal::from_offset(base(d), off));
    }
    static void set_root_offset(HermesCtr& d, AnyVal root) noexcept { d.set_root(root); }

    [[nodiscard]] static logos::expected<TinyObjectMap*> raw_tiny_map(HermesCtr& d, uint64_t cap = 4) noexcept { return d.make_tiny_map(cap); }
    [[nodiscard]] static logos::expected<ArrayView>      raw_array(HermesCtr& d, uint64_t cap = 4) noexcept { return d.make_array(cap); }
    [[nodiscard]] static logos::expected<MapView>        raw_object_map(HermesCtr& d, uint64_t cap = 8) noexcept { return d.make_object_map(cap); }
    [[nodiscard]] static logos::expected<StringView>     raw_string(HermesCtr& d, std::string_view s) noexcept { return d.make_string(s); }
};

}  // namespace logos::hermes2
