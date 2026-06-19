// Logos project — https://github.com/victor-smirnov/logos

#include <logos/verification/assert.hpp>
#include <logos/verification/call_chain.hpp>
#include <iostream>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <format>

namespace logos {

[[noreturn]] void __attribute__((no_instrument_function)) handle_assertion_failure(
    std::string_view req_id,
    std::string_view condition,
    std::string_view message,
    const char* file,
    int line)
{
    // Print to stderr
    std::cerr << "\n[LOGOS ASSERTION FAILURE]\n"
              << "Requirement: " << req_id << "\n"
              << "Location:    " << file << ":" << line << "\n"
              << "Condition:   " << condition << "\n"
              << "Message:     " << message << "\n\n"
              << format_call_chain() << "\n";

#ifndef NDEBUG
    std::abort();
#else
    resume_call_ring();
    throw std::runtime_error(std::string(req_id) + " assertion failed: " + std::string(message));
#endif
}

namespace {
    // Basic dynamic trace enabling
    bool g_traces_enabled = false;
    std::string g_trace_prefix;
}

bool __attribute__((no_instrument_function)) is_trace_enabled(std::string_view tag) noexcept {
    if (!g_traces_enabled) return false;
    if (tag.starts_with(g_trace_prefix)) return true;
    return false;
}

void __attribute__((no_instrument_function)) enable_trace(std::string_view tag_prefix) noexcept {
    if (tag_prefix == "*") {
        g_trace_prefix = "";
    } else {
        g_trace_prefix = tag_prefix;
        if (g_trace_prefix.ends_with(".*")) {
            g_trace_prefix = g_trace_prefix.substr(0, g_trace_prefix.size() - 2);
        }
    }
    g_traces_enabled = true;
}

void __attribute__((no_instrument_function)) write_trace(std::string_view /*tag*/, std::string_view /*json_data*/, const char* /*file*/, int /*line*/) noexcept {
    // The SQLite trace sink was removed; LOGOS_TRACE is now a no-op at the
    // backend. The is_trace_enabled() gate still applies at call sites.
}

} // namespace logos
