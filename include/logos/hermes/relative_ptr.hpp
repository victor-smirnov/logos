// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>

namespace logos::hermes {

// RelativePtr<T> — SELF-relative pointer (Hermes).
//
// Ported from Memoria's memoria::arena::RelativePtr (the original self-relative
// design; Logos's Hermes1 RelativePtr was a base-relative u32 SIMPLIFICATION of
// it). Stores the signed byte distance from the pointer's OWN storage address to
// the target (an i64); resolves as `(&this) + offset`. No segment base is threaded
// anywhere — the anchor is the field's own address, always at hand.
//
//     storage:  i64 offset           (8 bytes, at-rest in a never-move segment)
//     read:     T* = (&this) + offset
//     write:    offset = target − (&this)
//
// Sound precisely because Hermes segments NEVER MOVE (multi-chunk arena): a
// populated RelativePtr is meaningful at its storage location. Copy / move / `=`
// **re-anchor** — they recompute the offset relative to the destination's address
// so the copy points at the SAME target from its new location (this is what lets
// RelativePtr fields ride ordinary value-copies). A raw `memcpy` of the bytes does
// NOT re-anchor and is only valid as part of a rigid whole-segment relocation
// (compaction re-lowers every pointer explicitly). See docs/internals/
// hermes2-design.md §2.
//
// offset == 0 is the null sentinel (a real reference can never point a field at its
// own address); zoned objects are ≥2-aligned so a non-null offset's low bit is 0 —
// the invalid bit-patterns feed enum niche-packing (Option<zoned T>, HAny Ref|Pod).
template <typename T>
class RelativePtr {
    int64_t offset_;

public:
    RelativePtr() noexcept : offset_(0) {}

    RelativePtr(T* ptr) noexcept { assign(ptr); }

    // Copy / move RE-ANCHOR: recompute the offset from the destination address so
    // the copy resolves to the same target (NOT a raw byte copy of the offset).
    RelativePtr(const RelativePtr& other) noexcept { assign(other.get()); }
    RelativePtr(RelativePtr&& other) noexcept      { assign(other.get()); }
    RelativePtr& operator=(const RelativePtr& other) noexcept { assign(other.get()); return *this; }
    RelativePtr& operator=(RelativePtr&& other) noexcept      { assign(other.get()); return *this; }

    ~RelativePtr() noexcept = default;

    T* operator=(T* ptr) noexcept { assign(ptr); return ptr; }

    void reset() noexcept { offset_ = 0; }
    bool is_null() const noexcept { return offset_ == 0; }
    bool is_not_null() const noexcept { return offset_ != 0; }

    // Resolve to an absolute pointer (relative → absolute). No base needed. Returns
    // a mutable `T*` even from a const RelativePtr (the relptr's constness does not
    // imply the pointee's — matching Memoria/Hermes1's convention).
    T* get() const noexcept {
        return offset_ ? reinterpret_cast<T*>(my_addr() + offset_) : nullptr;
    }

    T* operator->() const noexcept { return get(); }

    // Raw at-rest offset (for serialization / niche inspection).
    int64_t offset() const noexcept { return offset_; }

private:
    // Lower an absolute pointer into the self-relative offset (absolute → relative).
    void assign(const T* ptr) noexcept {
        offset_ = ptr ? (reinterpret_cast<const uint8_t*>(ptr) - my_addr()) : 0;
    }

    uint8_t* my_addr() const noexcept {
        return const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(this));
    }
};

static_assert(sizeof(RelativePtr<int>) == sizeof(int64_t));

} // namespace logos::hermes
