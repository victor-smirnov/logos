// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <string_view>
#include <format>
#include <string>
#include <tuple>
#include <concepts>
#include <utility>
#include <logos/verification/call_chain.hpp>

namespace logos {

bool is_trace_enabled(std::string_view tag) noexcept;
void write_trace(std::string_view tag, std::string_view json_data, const char* file, int line) noexcept;
void enable_trace(std::string_view tag_prefix) noexcept;

namespace detail {

template<typename T>
[[gnu::no_instrument_function]] std::string format_json_value(const T& val) noexcept {
    if constexpr (std::is_constructible_v<std::string_view, T>) {
        return std::format("\"{}\"", val); // Note: proper escaping needed for production
    } else if constexpr (std::is_arithmetic_v<T>) {
        if constexpr (std::is_same_v<T, bool>) {
            return val ? "true" : "false";
        } else {
            return std::format("{}", val);
        }
    } else {
        // Fallback for pointers and other objects
        // In real C++23 this could use std::format if they implement std::formatter
        // We'll use a hack if not formatted, but expecting standard types for now.
        return "\"Opaque\"";
    }
}

template<typename... Args>
[[gnu::no_instrument_function]] std::string format_trace_data(Args&&... args) noexcept {
    constexpr size_t N = sizeof...(Args);
    static_assert(N % 2 == 0, "LOGOS_TRACE requires key-value pairs");
    
    std::string result = "{";
    auto t = std::forward_as_tuple(args...);
    
    auto format_pairs = [&]<std::size_t... Is>(std::index_sequence<Is...>) noexcept {
        bool first = true;
        ((
            result += (first ? "" : ","),
            first = false,
            result += std::format(R"("{}":{})", std::get<2 * Is>(t), format_json_value(std::get<2 * Is + 1>(t)))
        ), ...);
    };
    
    if constexpr (N > 0) {
        format_pairs(std::make_index_sequence<N / 2>{});
    }
    result += "}";
    return result;
}

} // namespace detail

template <typename... Args>
[[gnu::no_instrument_function]] void log_trace(std::string_view tag, const char* file, int line, Args&&... args) noexcept {
    if (!is_trace_enabled(tag)) return;
    ::logos::pause_call_ring();
    std::string json_data = detail::format_trace_data(std::forward<Args>(args)...);
    write_trace(tag, json_data, file, line);
    ::logos::resume_call_ring();
}

} // namespace logos

#define LOGOS_TRACE(tag, ...) \
    ::logos::log_trace(tag, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
