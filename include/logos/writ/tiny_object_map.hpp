// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <new>

#include <logos/writ/arena.hpp>
#include <logos/writ/relative_ptr.hpp>
#include <logos/writ/any_val.hpp>
#include <logos/writ/type_codes.hpp>
#include <logos/core/expected.hpp>

namespace logos::writ {

// TinyObjectMap — bitmap-indexed sparse map of small u8 keys (0..51) → AnyVal.
// BYTE-IDENTICAL to the Logos stdlib HMap<Hu6, HAny> (stdlib/lang/hermes2/hmap.logos
// "bitmap-indexed spec"):
//
//   { header_ : u64, schema_type_code_ : u64, data_ : self-relative ptr to AnyVal[] }
//     (24 bytes); header bits[0:51] = presence bitmap, [52:57] = capacity, [58:63] = size
//
// `schema_type_code_` is a node-class discriminator (0 = none). It is a FIRST-CLASS
// TOM field present in BOTH this C++ form AND the Logos stdlib HMap<Hu6,HAny> (the
// two share ONE byte layout). Hermes1 also carried it; it tags schema-shaped maps
// such as the multi-arena LirArenaRoot / ImportTable and metaprog ExprBlob roots,
// so a reader can recognise them without prior knowledge of the schema.
//
// NB: the value array is a SEPARATE buffer of
// at-rest AnyVals, kept in key order; O(1) lookup via popcount. FIXED capacity —
// `put` of a new key into a full map is a no-op (matches HMap<Hu6,HAny>::set).
class TinyObjectMap {
public:
    static constexpr uint64_t BITMAP_MASK = 0x000FFFFFFFFFFFFFull;  // bits[0:51]
    static constexpr uint8_t  MAX_KEYS    = 52;

    uint64_t bitmap()   const noexcept { return header_ & BITMAP_MASK; }
    uint64_t capacity() const noexcept { return (header_ >> 52) & 0x3F; }
    uint64_t size()     const noexcept { return (header_ >> 58) & 0x3F; }
    bool     empty()    const noexcept { return size() == 0; }

    // Node-class discriminator (0 = none). Set by schema-shaped maps (LirArenaRoot,
    // ImportTable, metaprog ExprBlob roots); byte-shared with Logos HMap<Hu6,HAny>.
    uint64_t schema_type_code() const noexcept { return schema_type_code_; }
    void     set_schema_type_code(uint64_t code) noexcept { schema_type_code_ = code; }

    bool has_key(uint8_t key) const noexcept {
        return key < MAX_KEYS && (bitmap() & (1ull << key)) != 0;
    }

    AnyVal get(uint8_t key) const noexcept {
        if (!has_key(key)) return AnyVal{};
        return elements()[index_of(key)];     // by-value copy re-anchors the Ref
    }
    // CUT-OVER VESTIGIAL (base ignored; self-relative needs none) — lets the logosc
    // base-threading call sites compile unchanged. Remove with the base-arg sweep.
    AnyVal get(uint8_t key, const void*) const noexcept { return get(key); }

    AnyVal* slot(uint8_t key) noexcept {
        return has_key(key) ? &elements()[index_of(key)] : nullptr;
    }

    // Insert or update `key → value`. key ≥ 52, or a new key into a full map, is a
    // no-op (returns ok). On a new key the value array is kept in key order.
    [[nodiscard]] logos::expected<void> put(uint8_t key, AnyVal value, Arena& /*arena*/) noexcept {
        if (key >= MAX_KEYS) return {};
        uint64_t bm  = bitmap();
        uint64_t kb  = 1ull << key;
        uint64_t pos = index_of(key);
        AnyVal*  buf = elements();
        if ((bm & kb) == 0) {                 // new key
            uint64_t sz = size(), cap = capacity();
            if (sz >= cap) return {};          // full — no growth (fixed capacity)
            for (uint64_t i = sz; i > pos; --i) buf[i] = buf[i - 1];  // shift (re-anchors)
            header_ = (bm | kb) | (cap << 52) | ((sz + 1) << 58);
        }
        buf[pos] = value;                      // assignment lowers (re-anchors) the Ref
        return {};
    }

    bool remove(uint8_t key) noexcept {
        if (!has_key(key)) return false;
        uint64_t bm = bitmap(), pos = index_of(key), sz = size(), cap = capacity();
        AnyVal* buf = elements();
        for (uint64_t i = pos; i + 1 < sz; ++i) buf[i] = buf[i + 1];  // shift left
        buf[sz - 1].set_null();
        header_ = (bm & ~(1ull << key)) | (cap << 52) | ((sz - 1) << 58);
        return true;
    }

    // Create an empty tiny map with `cap` (clamped to 52) value slots.
    [[nodiscard]] static logos::expected<TinyObjectMap*>
    create(Arena& arena, uint64_t cap) noexcept {
        if (cap > MAX_KEYS) cap = MAX_KEYS;
        TypeTag tag(tc::TINYMAP);
        LOGOS_TRY(auto* mem, arena.allocate(sizeof(TinyObjectMap), alignof(TinyObjectMap), tag));
        // GrowableSingleChunk REALLOCS on the buffer alloc below, which would dangle
        // `mem` — remember its offset and recompute after (no-op for never-move).
        size_t hdr_off = static_cast<size_t>(static_cast<uint8_t*>(mem) - arena.head().data());
        LOGOS_TRY(auto* buf_mem, arena.allocate_raw(cap * sizeof(AnyVal), alignof(AnyVal)));
        if (arena.mode() == ArenaMode::GrowableSingleChunk) mem = arena.head().data() + hdr_off;
        auto* m = new (mem) TinyObjectMap();
        auto* buf = static_cast<AnyVal*>(buf_mem);
        for (uint64_t i = 0; i < cap; ++i) new (&buf[i]) AnyVal();
        m->header_           = (cap << 52);    // bitmap 0, size 0, cap bits
        m->data_             = buf;
        m->schema_type_code_ = 0;              // no schema by default
        return m;
    }

private:
    // Layout matches Logos HMap<Hu6,HAny> byte-for-byte. The `#[zoned2]` pointer
    // (`data`) must be the LAST field (the Logos zoned-layout convention), so the
    // plain `schema_type_code_` word sits BETWEEN header and data.
    uint64_t            header_ = 0;
    uint64_t            schema_type_code_ = 0;
    RelativePtr<AnyVal> data_;

    AnyVal* elements() const noexcept { return data_.get(); }
    // Value-array position of `key` = popcount of present keys below it.
    uint64_t index_of(uint8_t key) const noexcept {
        return static_cast<uint64_t>(__builtin_popcountll(bitmap() & ((1ull << key) - 1)));
    }
};

// header_(8) + data_(8, self-relative i64) + schema_type_code_(8) = 24 (matches HMap<Hu6,HAny>)
static_assert(sizeof(TinyObjectMap) == 24);

} // namespace logos::writ
