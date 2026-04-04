// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// expected.hpp — logos::expected<T, E>
//
// Thin wrapper over std::expected<T, E> that adds:
//   .get()  — returns the value or calls std::terminate() on error.
//
// Designed for -fno-exceptions builds.  get() is a "forced unwrap" —
// use it only at test/exerciser boundaries where an error is a bug.
// All library code propagates errors via LOGOS_TRY / LOGOS_TRY_VOID.
//
// Default error type is logos::Err — write logos::expected<int> for brevity.

#pragma once

#include <expected>
#include <utility>
#include <exception>
#include <logos/core/err.hpp>

namespace logos {

// ---------------------------------------------------------------------------
// Primary template
// ---------------------------------------------------------------------------
template<typename T, typename E = Err>
class expected : public std::expected<T, E> {
    using Base = std::expected<T, E>;
public:
    using Base::Base;
    using Base::operator=;

    // Return value or terminate.  Use only at test/exerciser boundaries.
    [[gnu::always_inline]] inline
    T& get() & noexcept {
        if (!this->has_value()) [[unlikely]]
            std::terminate();
        return **this;
    }

    [[gnu::always_inline]] inline
    T get() && noexcept {
        if (!this->has_value()) [[unlikely]]
            std::terminate();
        return std::move(**this);
    }
};

// ---------------------------------------------------------------------------
// Specialisation for expected<void, E>
// ---------------------------------------------------------------------------
template<typename E>
class expected<void, E> : public std::expected<void, E> {
    using Base = std::expected<void, E>;
public:
    using Base::Base;
    using Base::operator=;

    [[gnu::always_inline]] inline
    void get() & noexcept {
        if (!this->has_value()) [[unlikely]]
            std::terminate();
    }

    [[gnu::always_inline]] inline
    void get() && noexcept {
        if (!this->has_value()) [[unlikely]]
            std::terminate();
    }
};

} // namespace logos

// ---------------------------------------------------------------------------
// Error-propagation macros
// ---------------------------------------------------------------------------
//
// LOGOS_TRY(decl, expr)
//   Evaluates expr (must return logos::expected or std::expected).
//   On error: returns std::unexpected(error) from the enclosing function.
//   On success: declares `decl` initialised with the value.
//
//   LOGOS_TRY(auto val,   inner());   // declares val in current scope
//   LOGOS_TRY(auto& val,  inner());   // binds reference (rarely needed)
//
// LOGOS_TRY_VOID(expr)
//   Same, but for expected<void, E> or when the value is discarded.
//   On error: returns std::unexpected(error).
//   On success: continues.
//
// Both macros generate optimal code: a single conditional branch + tail call
// for the error path, zero EH overhead on the happy path.
//
// Note: unique variable names use __LINE__ — do not call two macros on the
// same physical line (e.g. separated by a comma or semicolon).
// Both macros expand to two statements — always use braces around if/else
// bodies that contain them (unbraced single-statement if would only capture
// the first statement of the expansion).

#define LOGOS_PP_CAT2(a, b) a##b
#define LOGOS_PP_CAT(a, b)  LOGOS_PP_CAT2(a, b)

#define LOGOS_TRY(decl, expr)                                              \
    auto LOGOS_PP_CAT(_logos_r_, __LINE__) = (expr);                      \
    if (!LOGOS_PP_CAT(_logos_r_, __LINE__)) [[unlikely]]                  \
        return std::unexpected(                                            \
            std::move(LOGOS_PP_CAT(_logos_r_, __LINE__).error()));        \
    decl = std::move(*LOGOS_PP_CAT(_logos_r_, __LINE__))

#define LOGOS_TRY_VOID(expr)                                               \
    auto LOGOS_PP_CAT(_logos_rv_, __LINE__) = (expr);                     \
    if (!LOGOS_PP_CAT(_logos_rv_, __LINE__)) [[unlikely]]                 \
        return std::unexpected(                                            \
            std::move(LOGOS_PP_CAT(_logos_rv_, __LINE__).error()))

