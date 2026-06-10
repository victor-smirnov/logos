// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>

namespace logos::hermes2 {

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

    // The raw at-rest word (for serialization / niche inspection).
    int64_t raw() const noexcept { return word_; }

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

} // namespace logos::hermes2
