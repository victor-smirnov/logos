// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <type_traits>
#include <logos/hermes/config.hpp>     // arena_offset_t (for the mirror's base+offset handles)
#include <logos/hermes/type_codes.hpp> // tc::* (Pod code deduction for from_value<T>)

namespace logos::hermes {

// AnyVal — the Hermes2 heterogeneous slot. ONE 8-byte word, BYTE-IDENTICAL to the
// Logos stdlib `HAny` (stdlib/lang/hermes2/anyval.logos) — both sides read the same
// bytes (shared wire/disk layout). The 4-byte base-relative AnyVal of Hermes1 is
// replaced by this 8-byte self-relative niche.
//
//   word == 0          → null  (= Ref(0))
//   word & 1 == 1      → Pod : bits[7:1] = 7-bit type code, bits[63:8] = i56 value
//                              (= `(value<<8) | ((code&0x7F)<<1) | 1`).  Position-
//                              independent — copies verbatim.
//   word & 1 == 0 (≠0) → Ref : a SELF-RELATIVE delta `target − &this` to a tagged
//                              zone object.  Resolved in place via `resolve()`.
//                              Zone objects are ≥2-aligned, so a Ref's low bit is 0.
//
// Like RelativePtr / Memoria's EmbeddingRelativePtr, the Ref arm is anchored to the
// AnyVal's own address; copy / move / `=` RE-ANCHOR it (resolve from the source,
// lower at the destination) so an AnyVal rides ordinary value-copies. (Pod / null
// copy verbatim.) A raw `memcpy` does NOT re-anchor and is valid only as part of a
// rigid whole-segment relocation (compaction re-lowers explicitly).
class AnyVal {
    int64_t word_;

public:
    AnyVal() noexcept : word_(0) {}   // null

    // Copy / move RE-ANCHOR the Ref arm.
    AnyVal(const AnyVal& o) noexcept { assign(o); }
    AnyVal(AnyVal&& o) noexcept      { assign(o); }
    AnyVal& operator=(const AnyVal& o) noexcept { assign(o); return *this; }
    AnyVal& operator=(AnyVal&& o) noexcept      { assign(o); return *this; }
    ~AnyVal() noexcept = default;

    // ── constructors ──────────────────────────────────────────────────────────
    static AnyVal null() noexcept { return AnyVal{}; }

    // Inline primitive: `code` is the 7-bit Hermes2 type code (1..127); `v` must fit
    // 56 signed bits (wider primitives box into a Ref).
    static AnyVal pod(int64_t v, uint8_t code) noexcept {
        AnyVal a;
        a.word_ = (v << 8) | ((static_cast<int64_t>(code) & 0x7F) << 1) | 1;
        return a;
    }
    static AnyVal pod_bool(bool b, uint8_t code) noexcept { return pod(b ? 1 : 0, code); }

    // ── discriminants ─────────────────────────────────────────────────────────
    bool is_null() const noexcept { return word_ == 0; }
    bool is_pod()  const noexcept { return (word_ & 1) == 1; }
    bool is_ref()  const noexcept { return (word_ & 1) == 0 && word_ != 0; }

    // Hermes1-spelling aliases (a Ref is the self-relative successor of Hermes1's
    // base-relative "pointer"; a Pod is its inline "value"). Kept so the logosc
    // cut-over is a near-mechanical rename — NO base/offset model is reintroduced.
    bool is_pointer() const noexcept { return is_ref(); }
    bool is_value()   const noexcept { return is_pod(); }

    // ── Pod accessors (position-independent reads) ──────────────────────────────
    uint8_t pod_code() const noexcept { return static_cast<uint8_t>((word_ >> 1) & 0x7F); }
    int64_t as_i56()   const noexcept { return word_ >> 8; }   // arithmetic shift → sign-extends
    bool    as_bool()  const noexcept { return (word_ >> 8) != 0; }

    // Typed inline-value accessors (the i56 payload narrowed to T). `value_type_hash`
    // is the Hermes1 spelling of `pod_code`.
    template <typename T>
    T as_value() const noexcept { return static_cast<T>(as_i56()); }
    uint8_t value_type_hash() const noexcept { return pod_code(); }

    // Encode a small typed value into a Pod niche with type code `code`.
    template <typename T>
    static AnyVal from_value(T v, uint8_t code) noexcept {
        return pod(static_cast<int64_t>(v), code);
    }

    // 1-arg from_value — deduce the Pod type code from T (the Hermes1 overload that
    // read TypeTraits<T>::hash). Readers narrow via as_value<T>() so the exact code is
    // not load-bearing, but match the natural width for fidelity.
    template <typename T>
    static constexpr uint8_t code_for() noexcept {
        if constexpr (std::is_same_v<T, bool>)         return uint8_t(tc::HA_BOOL);
        else if constexpr (std::is_same_v<T, uint8_t>) return uint8_t(tc::HT_U8);
        else if constexpr (std::is_same_v<T, int8_t>)  return uint8_t(tc::HT_I8);
        else if constexpr (std::is_same_v<T, uint16_t>)return uint8_t(tc::HT_U16);
        else if constexpr (std::is_same_v<T, int16_t>) return uint8_t(tc::HT_I16);
        else if constexpr (std::is_unsigned_v<T>)      return uint8_t(tc::HT_U24);
        else                                           return uint8_t(tc::HT_I24);
    }
    template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
    static AnyVal from_value(T v) noexcept { return pod(static_cast<int64_t>(v), code_for<T>()); }

    // NamedCode-like (a `.code` integral member, e.g. logos::NamedCode<int32_t>) —
    // duck-typed so any_val.hpp needs no NamedCode include.
    template <typename NC, typename = decltype(NC::code), typename = std::enable_if_t<!std::is_integral_v<NC>>>
    static AnyVal from_value(NC nc) noexcept { return from_value(nc.code); }

    // ── Ref access ──────────────────────────────────────────────────────────────
    // Lower an absolute pointer into this slot's self-relative Ref (absolute → rel).
    void set_ref(const void* target) noexcept {
        word_ = target ? (reinterpret_cast<const uint8_t*>(target) - my_addr()) : 0;
    }
    void set_null() noexcept { word_ = 0; }

    // Resolve a Ref to its absolute target (relative → absolute). Null/Pod → nullptr.
    const uint8_t* resolve() const noexcept {
        return is_ref() ? (my_addr() + word_) : nullptr;
    }

    // Native typed pointer to the Ref target (NO base — self-relative resolves in
    // place). The Hermes1 cut-over drops the old `base` argument at every call site.
    template <typename T> T* as_ptr() const noexcept {
        return reinterpret_cast<T*>(const_cast<uint8_t*>(resolve()));
    }
    // CUT-OVER VESTIGIAL (base ignored) — lets logosc as_ptr(base) sites compile.
    template <typename T> T* as_ptr(const void*) const noexcept { return as_ptr<T>(); }

    // Lower an absolute pointer into this slot's Ref (the cut-over successor of the
    // Hermes1 base-relative `set_pointer(target, base)` — base no longer needed).
    void set_pointer(const void* target) noexcept { set_ref(target); }

    // The single-segment (GrowableSingleChunk) arena offset of this Ref's target,
    // relative to `base` (= arena.head().data()). Used by the LIR mirror / TypePool
    // handles, which store realloc-safe arena_offset_t (not absolute pointers, which
    // would dangle when the single chunk reallocs on grow). Resolve the inverse with
    // `from_offset(base, off)`.
    arena_offset_t to_offset(const uint8_t* base) const noexcept {
        return arena_offset_t(static_cast<uint32_t>(resolve() - base));
    }
    // Build a Ref AnyVal from a (base, offset) pair (the inverse of to_offset(base)).
    static AnyVal from_offset(const uint8_t* base, arena_offset_t off) noexcept {
        AnyVal a; a.set_ref(base + off.value()); return a;
    }

    // The raw at-rest word (for serialization / niche inspection).
    int64_t raw() const noexcept { return word_; }
    static AnyVal from_raw(int64_t w) noexcept { AnyVal a; a.word_ = w; return a; }

private:
    void assign(const AnyVal& o) noexcept {
        if ((o.word_ & 1) == 1 || o.word_ == 0) {
            word_ = o.word_;                       // Pod or null: position-independent
        } else {
            const uint8_t* target = o.my_addr() + o.word_;   // resolve at source
            word_ = target - my_addr();                      // re-lower at destination
        }
    }
    uint8_t* my_addr() const noexcept {
        return const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(this));
    }
};

static_assert(sizeof(AnyVal) == 8);

} // namespace logos::hermes
