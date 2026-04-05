// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <string_view>
#include <cstdint>
#include <string>

namespace logos {

struct TraceDatabaseConfig {
    std::string path = "logos_trace.sqlite";
};

// Initialize the trace sink (schema, threads)
void init_sqlite_sink(const TraceDatabaseConfig& config) noexcept;
// Graceful shutdown
void shutdown_sqlite_sink() noexcept;

// Submit an assertion failure record
void record_assertion(
    uint64_t timestamp_ns,
    uint64_t thread_id,
    uint64_t fiber_id,  // 0 if unknown
    std::string_view req_id,
    std::string_view condition,
    std::string_view message,
    const char* file,
    int line,
    std::string_view call_chain_json) noexcept;

// Submit a trace record
void record_trace(
    uint64_t timestamp_ns,
    uint64_t thread_id,
    uint64_t fiber_id,
    std::string_view tag,
    const char* file,
    int line,
    std::string_view data_json) noexcept;

} // namespace logos
