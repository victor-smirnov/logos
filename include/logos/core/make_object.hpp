// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// make_object.hpp — factory for objects with fallible construction.
//
// Constructs T with InitTag as the first argument, then returns either:
//   - logos::expected<T>              if T is move-constructible
//   - logos::expected<unique_ptr<T>>  otherwise
//
// The constructor signals failure via tag.fail(err). If construction
// succeeds (tag.ok()), the object is returned; otherwise the error is.
//
// Usage:
//   auto sock = logos::make_object<TcpSocket>(host, port);
//   // → logos::expected<TcpSocket>
//
//   auto srv  = logos::make_object<MyImmovableService>(config);
//   // → logos::expected<std::unique_ptr<MyImmovableService>>

#pragma once

#include <logos/core/expected.hpp>
#include <memory>
#include <type_traits>
#include <utility>

namespace logos {

template<typename T, typename... Args>
auto make_object(Args&&... args) {
    InitTag tag;
    if constexpr (std::is_move_constructible_v<T>) {
        T obj(tag, std::forward<Args>(args)...);
        if (!tag.ok())
            return logos::expected<T>(std::unexpected(std::move(tag.err)));
        return logos::expected<T>(std::move(obj));
    } else {
        // T is not movable — allocate on heap, return unique_ptr.
        auto ptr = std::make_unique<T>(tag, std::forward<Args>(args)...);
        if (!tag.ok())
            return logos::expected<std::unique_ptr<T>>(
                std::unexpected(std::move(tag.err)));
        return logos::expected<std::unique_ptr<T>>(std::move(ptr));
    }
}

} // namespace logos
