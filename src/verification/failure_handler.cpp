#include <logos/verification/assert.hpp>
#include <logos/verification/call_chain.hpp>
#include <logos/verification/sqlite_sink.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <stdexcept>
#include <string_view>

namespace logos {

[[noreturn]] void __attribute__((no_instrument_function)) handle_assertion_failure(
    std::string_view req_id,
    std::string_view condition,
    std::string_view message,
    const std::source_location& loc)
{
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    
    // Simplified thread ID cast
    uint64_t thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
    uint64_t fiber_id = 0; // Fiber ID not implemented yet
    
    std::string stack_json = capture_call_chain_json();
    
    // Print to stderr
    std::cerr << "\n[LOGOS ASSERTION FAILURE]\n"
              << "Requirement: " << req_id << "\n"
              << "Location:    " << loc.file_name() << ":" << loc.line() << "\n"
              << "Condition:   " << condition << "\n"
              << "Message:     " << message << "\n\n";
              
    // Write to SQLite
    record_assertion(timestamp, thread_id, fiber_id, req_id, condition, message, loc, stack_json);
    
    // Give time to flush before abort
    shutdown_sqlite_sink();
    
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

bool __attribute__((no_instrument_function)) is_trace_enabled(std::string_view tag) {
    if (!g_traces_enabled) return false;
    if (tag.starts_with(g_trace_prefix)) return true;
    return false;
}

void __attribute__((no_instrument_function)) enable_trace(std::string_view tag_prefix) {
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

void __attribute__((no_instrument_function)) write_trace(std::string_view tag, std::string_view json_data, const std::source_location& loc) {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    uint64_t thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
    
    record_trace(timestamp, thread_id, 0, tag, loc, json_data);
}

} // namespace logos
