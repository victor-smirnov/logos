// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/verification/assert.hpp>
#include <logos/verification/trace.hpp>
#include <logos/verification/sqlite_sink.hpp>
#include <iostream>

[[gnu::noinline]] void deeper_function() noexcept {
    LOGOS_TRACE("test.deep", "msg", "Inside deeper function", "val", 42);
    // Simulate an error
    int x = 5;
    LOGOS_ASSERT(x == 10, "REQ-001", "Expected x to be 10, got {}", x);
}

[[gnu::noinline]] void some_function() noexcept {
    LOGOS_TRACE("test.mid", "step", 1);
    deeper_function();
}

int main() {
    std::cout << "Initializing SQLite Trace Sink...\n";
    logos::TraceDatabaseConfig config{"test_traces.sqlite"};
    logos::init_sqlite_sink(config);
    
    logos::enable_trace("test.*");
    
    LOGOS_TRACE("test.start", "event", "start up", "status", "ok");
    
    std::cout << "Calling some_function()...\n";
    some_function();
    
    logos::shutdown_sqlite_sink();
    return 0;
}
