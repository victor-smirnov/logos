// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <vector>
#include <cstdint>
#include <string>

namespace logos {

struct CallEvent {
    void* func_addr;
    void* call_site;
    uint64_t timestamp_ns;
};

// Returns a copy of the current thread's ring buffer (in logical order, oldest to newest)
std::vector<CallEvent> get_thread_call_chain();

// Returns a JSON array string of serialized symbolified call chain
std::string capture_call_chain_json();

// Returns a human-readable formatted call chain string
std::string format_call_chain();

// Pause and resume call ring tracking (useful to prevent std::format from overwriting the chain on assert)
void pause_call_ring() noexcept;
void resume_call_ring() noexcept;

} // namespace logos
