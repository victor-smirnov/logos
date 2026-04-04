// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// err.hpp — Logos error type.
//
// Err is a 64-bit move-only value that holds either:
//   - a scalar error code  (bit 0 = 1, code = bits >> 1)
//   - a heap ErrObj*       (bit 0 = 0, bits != 0)
//   - empty / no error     (bits == 0)
//
// Heap alignment is guaranteed to be ≥ 2 bytes so bit 0 is always 0 in a
// valid pointer — the tag bit is unambiguous.
//
// Scalar error code ranges (per subsystem):
//   0x0000'0000'0000'0000  — empty (not a real error)
//   0x0000'0000'0000'0001 … 0x0000'0000'0000'FFFF  — core / generic
//   0x0000'0000'0001'0000 … 0x0000'0000'0001'FFFF  — hermes
//   0x0000'0000'0002'0000 … 0x0000'0000'0002'FFFF  — hrpc
//   0x0000'0000'0003'0000 … 0x0000'0000'0003'FFFF  — reactor
//   (higher ranges reserved for future subsystems)
//
// Err is throwable: throw std::move(err);
// Always catch by reference:  catch (Err& e) { ... }

#pragma once

#include <cstdint>
#include <string_view>
#include <utility>

namespace logos {

// ---------------------------------------------------------------------------
// ErrObj — base class for heap-allocated error objects.
// Derive from this to attach structured data (message, context, chain, etc.)
// ---------------------------------------------------------------------------
struct ErrObj {
    virtual ~ErrObj() = default;

    // Human-readable description of the error.
    virtual std::string_view message() const noexcept = 0;
};

// ---------------------------------------------------------------------------
// Err — 64-bit error value.  Move-only.
// ---------------------------------------------------------------------------
class Err {
    uint64_t bits_ = 0;

    [[gnu::always_inline]]
    void reset() noexcept {
        if (is_heap()) delete reinterpret_cast<ErrObj*>(bits_);
        bits_ = 0;
    }

public:
    // --- Construction ---

    Err() noexcept = default;

    // Scalar error code.
    [[gnu::always_inline]]
    static Err from_code(uint64_t code) noexcept {
        Err e;
        e.bits_ = (code << 1) | 1;
        return e;
    }

    // Heap error object — Err takes ownership.
    [[gnu::always_inline]]
    static Err from_obj(ErrObj* obj) noexcept {
        Err e;
        e.bits_ = reinterpret_cast<uint64_t>(obj);
        return e;
    }

    // Construct a heap ErrObj of type T in-place.
    template<typename T, typename... Args>
    [[gnu::always_inline]]
    static Err make(Args&&... args) {
        return from_obj(new T(std::forward<Args>(args)...));
    }

    // --- Move semantics (copy is deleted — Err is not refcounted) ---

    Err(Err&& o) noexcept : bits_(o.bits_) { o.bits_ = 0; }

    Err& operator=(Err&& o) noexcept {
        if (this != &o) { reset(); bits_ = o.bits_; o.bits_ = 0; }
        return *this;
    }

    Err(const Err&)            = delete;
    Err& operator=(const Err&) = delete;

    ~Err() noexcept { reset(); }

    // --- Inspection ---

    [[gnu::always_inline]] bool empty()     const noexcept { return bits_ == 0; }
    [[gnu::always_inline]] bool is_scalar() const noexcept { return  (bits_ & 1); }
    [[gnu::always_inline]] bool is_heap()   const noexcept { return !(bits_ & 1) && bits_ != 0; }

    // Valid only when is_scalar().
    [[gnu::always_inline]] uint64_t code() const noexcept { return bits_ >> 1; }

    // Valid only when is_heap().
    [[gnu::always_inline]] const ErrObj* obj() const noexcept {
        return reinterpret_cast<const ErrObj*>(bits_);
    }

    [[gnu::always_inline]] ErrObj* obj() noexcept {
        return reinterpret_cast<ErrObj*>(bits_);
    }

    // Human-readable description.
    [[gnu::always_inline]]
    std::string_view message() const noexcept {
        if (is_heap())   return obj()->message();
        if (is_scalar()) return "(scalar error)";
        return "(empty)";
    }
};

// ---------------------------------------------------------------------------
// Convenience: build a scalar Err from a typed code enum/constant.
// Usage:  return logos::err(hermes::ErrCode::not_found);
// ---------------------------------------------------------------------------
template<typename Code>
[[gnu::always_inline]] inline
Err err(Code code) noexcept {
    return Err::from_code(static_cast<uint64_t>(code));
}

// ---------------------------------------------------------------------------
// InitTag — passed as first argument to fallible constructors.
//
// The constructor signals failure by calling tag.fail(e).
// make_object<T>(args...) creates the InitTag, constructs T, and checks it.
//
// Convention: InitTag& is always the first constructor parameter.
//
// Example:
//   class Foo {
//   public:
//       Foo(logos::InitTag& tag, int x) noexcept {
//           if (x < 0) { tag.fail(logos::err(ErrCode::bad_arg)); return; }
//           // ...
//       }
//   };
//   auto foo = logos::make_object<Foo>(42);  // → logos::expected<Foo>
// ---------------------------------------------------------------------------
struct InitTag {
    Err err;

    bool ok() const noexcept { return err.empty(); }

    void fail(Err e) noexcept { err = std::move(e); }
};

} // namespace logos
