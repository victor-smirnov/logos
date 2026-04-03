// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// expected.hpp — logos::expected<T, E>
//
// Thin wrapper over std::expected<T, E> that adds:
//   .get()  — returns the value or throws E directly (not bad_expected_access).
//
// get() is marked always_inline so that when it is inlined into a noexcept
// barrier function, the compiler sees the throw/catch pair in one function and
// can perform EH elision (converting them to a plain conditional branch).
//
// Usage:
//   logos::expected<int, Err> foo() noexcept {
//       try {
//           return compute().get();   // throws Err if error
//       }
//       catch (Err& e) {
//           return std::unexpected(std::move(e));
//       }
//   }
//
// Default error type is logos::Err — write logos::expected<int> for brevity.

#pragma once

#include <expected>
#include <utility>
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

    // Return value or throw E.  Non-const only — throwing moves the error out.
    [[gnu::always_inline]] inline
    T& get() & {
        if (!this->has_value()) [[unlikely]]
            throw std::move(this->error());
        return **this;
    }

    [[gnu::always_inline]] inline
    T get() && {
        if (!this->has_value()) [[unlikely]]
            throw std::move(this->error());
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
    void get() & {
        if (!this->has_value()) [[unlikely]]
            throw std::move(this->error());
    }

    [[gnu::always_inline]] inline
    void get() && {
        if (!this->has_value()) [[unlikely]]
            throw std::move(this->error());
    }
};

} // namespace logos
