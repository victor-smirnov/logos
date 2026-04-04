// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/verification/call_chain.hpp>
#include <chrono>
#include <dlfcn.h>
#include <cxxabi.h>
#include <format>
#include <memory>

namespace logos {

constexpr size_t RING_BUFFER_SIZE = 256;

struct RingBuffer {
    CallEvent events[RING_BUFFER_SIZE];
    size_t head = 0;
    bool wrapped = false;
};

thread_local RingBuffer t_call_ring;
thread_local bool t_call_ring_paused = false;

static uint64_t get_time_ns() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

extern "C" void __attribute__((no_instrument_function))
__cyg_profile_func_enter(void *this_fn, void *call_site) noexcept {
    if (t_call_ring_paused) return;
    
    auto& ring = t_call_ring;
    auto idx = ring.head % RING_BUFFER_SIZE;
    ring.events[idx] = CallEvent{this_fn, call_site, get_time_ns()};
    ring.head++;
    if (ring.head >= RING_BUFFER_SIZE) {
        ring.wrapped = true;
    }
}

extern "C" void __attribute__((no_instrument_function))
__cyg_profile_func_exit(void *this_fn, void *call_site) noexcept {
    // For now, we only trace entries to see the execution path.
    // Tracing exits as well would require differentiating them in the event struct,
    // which the spec doesn't explicitly mandate for the MVP.
    (void)this_fn;
    (void)call_site;
}

std::vector<CallEvent> __attribute__((no_instrument_function)) get_thread_call_chain() {
    auto& ring = t_call_ring;
    std::vector<CallEvent> result;
    
    size_t count = ring.wrapped ? RING_BUFFER_SIZE : ring.head;
    result.reserve(count);
    
    size_t start = ring.wrapped ? (ring.head % RING_BUFFER_SIZE) : 0;
    for (size_t i = 0; i < count; ++i) {
        size_t idx = (start + i) % RING_BUFFER_SIZE;
        result.push_back(ring.events[idx]);
    }
    
    return result;
}

static std::string __attribute__((no_instrument_function)) demangle(const char* name) {
    if (!name) return "";
    int status = -1;
    std::unique_ptr<char, void(*)(void*)> res{
        abi::__cxa_demangle(name, nullptr, nullptr, &status),
        std::free
    };
    return (status == 0) ? res.get() : name;
}

std::string __attribute__((no_instrument_function)) capture_call_chain_json() {
    auto chain = get_thread_call_chain();
    std::string json = "[";
    bool first = true;
    
    for (const auto& ev : chain) {
        Dl_info info;
        std::string func_name = "unknown";
        if (dladdr(ev.func_addr, &info) && info.dli_sname) {
            func_name = demangle(info.dli_sname);
        }
        
        if (!first) json += ",";
        first = false;
        
        // Escape quotes securely if needed, but assuming demangled names are mostly safe
        // A robust JSON escape is needed for production.
        json += std::format(R"({{"func":"{}","addr":"{}","site":"{}"}})", 
            func_name, ev.func_addr, ev.call_site);
    }
    json += "]";
    return json;
}

void __attribute__((no_instrument_function)) pause_call_ring() noexcept {
    t_call_ring_paused = true;
}

void __attribute__((no_instrument_function)) resume_call_ring() noexcept {
    t_call_ring_paused = false;
}

} // namespace logos
