// Logos project — https://github.com/victor-smirnov/logos
//
// Writ SPELLING-compat surface for the logosc cut-over (Phase B). Provides the
// legacy NAMES the compiler uses (type code constants, the doc-handle / Object
// spellings, a WritAccess shim) mapped onto NATIVE writ — NO base/offset model
// is reintroduced (self-relative throughout). Transitional: these spellings can be
// swept to the native tc::/WritCtr/view forms later. Include this from compiler TUs.

#pragma once

// Umbrella — pulls in the whole writ surface the compiler needs, so every
// `#include <logos/writ/*.hpp>` in logosc maps to this one header during the cut-over.
#include <logos/writ/document.hpp>
#include <logos/writ/view.hpp>
#include <logos/writ/type_codes.hpp>
#include <logos/writ/schema_codes.hpp>
#include <logos/writ/any_val.hpp>
#include <logos/writ/arena.hpp>
#include <logos/writ/type_tag.hpp>
#include <logos/writ/config.hpp>
#include <logos/writ/mem_holder.hpp>
#include <logos/writ/tiny_object_map.hpp>
#include <logos/writ/object_array.hpp>
#include <logos/writ/object_map.hpp>
#include <logos/writ/typed_array.hpp>
#include <logos/writ/map.hpp>
#include <logos/writ/arena_string.hpp>
#include <logos/writ/compound_types.hpp>
#include <logos/writ/clone.hpp>
#include <logos/writ/binary_codec.hpp>
#include <logos/writ/external_ref.hpp>
#include <logos/writ/arena_pool.hpp>
#include <logos/writ/import_table.hpp>
#include <logos/writ/arena_publish.hpp>
#include <logos/writ/lir_arena_root.hpp>

namespace logos::writ {

// ── legacy type_hash:: code names → writ tc:: values ────────────────────────
// Structural in-band tags + Pod codes the compiler compares against. NOTE the two
// that CHANGED value: WritString 28→130 and Bool 37→2 (the writ wire codes).
namespace type_hash {
inline constexpr uint64_t WritString  = tc::STRING;     // 130 (legacy used 28)
inline constexpr uint64_t Bool          = tc::WA_BOOL;    // 2   (legacy used 37)
inline constexpr uint64_t TinyObjectMap = tc::TINYMAP;    // 98
inline constexpr uint64_t Array         = tc::ARRAY;      // 100
inline constexpr uint64_t ObjectMap     = tc::MAP;        // 101
inline constexpr uint64_t Type          = 107;            // schema_type_code value (verbatim)
inline constexpr uint64_t U24           = tc::WT_U24;     // 25
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

// Reflection param slot (ported from legacy clone.hpp).
struct ParamSlot {
    uint32_t offset;
    uint32_t value_index;
};

// the legacy type-ops registry init — writ has no per-type ops vtable (clone/
// stringify dispatch directly on the TypeTag), so this is a no-op.
inline void writ_init() noexcept {}

// ── Doc-handle spellings ─────────────────────────────────────────────────────────
// (Object lives in view.hpp; WritCtr::root_object() returns it.)
using WritView = WritCtr;
using Writ     = WritCtr;

// Load a document from a compacted blob (legacy spelling of WritCtr::from_bytes).
[[nodiscard]] inline logos::expected<WritCtr>
from_bytes_copy(const uint8_t* data, size_t size) noexcept {
    return WritCtr::from_bytes(data, size);
}

// Deep-copy a tagged object from another document into `dst` (the metaprog blob splice).
// `src_base` is vestigial (writ is self-relative; src_obj is an absolute pointer the
// deep-copy walks via resolve()). Returns the dst object pointer.
[[nodiscard]] inline logos::expected<void*>
copy_object_into(const void* src_obj, const uint8_t* /*src_base*/, WritCtr& dst) noexcept {
    DeepCopyState st(dst.holder());
    void* d = deep_copy_object(reinterpret_cast<const uint8_t*>(src_obj), st);
    if (!d) return std::unexpected(logos::err(ErrCode::out_of_memory));
    return d;
}

// Box a wide scalar (i64/u64/f32/f64 — doesn't fit the inline Pod) into the arena and
// return a Ref AnyVal to it (legacy anyval_put / arena_put). Tag = the matching tc code.
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

// ── WritAccess shim — native (no base/offset model) ───────────────────────────
// raw_* + arena map to WritCtr methods. base() is VESTIGIAL (the head chunk start)
// and is only ever fed to AnyVal::as_ptr(base)/set_pointer(target,base) call sites,
// which IGNORE it (self-relative). Sweep those base args away in the cut-over.
class WritAccess {
public:
    static Arena& arena(WritCtr& d) noexcept { return d.arena(); }
    static Arena& arena(const WritCtr& d) noexcept { return const_cast<WritCtr&>(d).arena(); }
    static uint8_t* base(const WritCtr& d) noexcept { return d.holder()->arena().head().data(); }

    // Root offset accessors (the mirror/TypePool address by offset within their single
    // chunk). root() returns the root AnyVal; its offset is relative to base().
    static arena_offset_t root_offset(const WritCtr& d) noexcept {
        AnyVal r = d.root();
        return r.is_ref() ? r.to_offset(base(d)) : NULL_OFFSET;
    }
    static void set_root_offset(WritCtr& d, arena_offset_t off) noexcept {
        d.set_root(off == NULL_OFFSET ? AnyVal{} : AnyVal::from_offset(base(d), off));
    }
    static void set_root_offset(WritCtr& d, AnyVal root) noexcept { d.set_root(root); }

    [[nodiscard]] static logos::expected<TinyObjectMap*> raw_tiny_map(WritCtr& d, uint64_t cap = 4) noexcept { return d.make_tiny_map(cap); }
    [[nodiscard]] static logos::expected<ArrayView>      raw_array(WritCtr& d, uint64_t cap = 4) noexcept { return d.make_array(cap); }
    [[nodiscard]] static logos::expected<MapView>        raw_object_map(WritCtr& d, uint64_t cap = 8) noexcept { return d.make_object_map(cap); }
    [[nodiscard]] static logos::expected<StringView>     raw_string(WritCtr& d, std::string_view s) noexcept { return d.make_string(s); }
};

}  // namespace logos::writ
