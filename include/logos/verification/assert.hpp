// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <source_location>
#include <format>
#include <string_view>
#include <logos/verification/call_chain.hpp>

namespace logos {

[[noreturn]] void handle_assertion_failure(
    std::string_view req_id,
    std::string_view condition,
    std::string_view message,
    const std::source_location& loc);

} // namespace logos

#define LOGOS_ASSERT(condition, req_id, format_str, ...) \
    do { \
        if (!(condition)) [[unlikely]] { \
            ::logos::pause_call_ring(); \
            auto msg = std::format(format_str __VA_OPT__(,) __VA_ARGS__); \
            ::logos::handle_assertion_failure(req_id, #condition, msg, std::source_location::current()); \
        } \
    } while (false)
