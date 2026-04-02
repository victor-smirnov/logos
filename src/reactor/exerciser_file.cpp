// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Layer 3 exerciser: async file IO via io_uring.

#include <logos/reactor/file.hpp>
#include <logos/reactor/reactor.hpp>
#include <logos/verification/assert.hpp>
#include <logos/verification/trace.hpp>

#include <fcntl.h>
#include <unistd.h>
#include <filesystem>
#include <print>
#include <string>
#include <vector>

using namespace logos::reactor;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Test 1: write a file, read it back, verify contents
// ---------------------------------------------------------------------------
static void test_file_write_read() {
    LOGOS_TRACE("reactor.file.write_read", "start", "");
    const char* path = "/tmp/logos_reactor_test_wr.bin";

    Reactor reactor;
    std::vector<uint8_t> written;
    std::vector<uint8_t> read_back;

    reactor.spawn([&] {
        // Write 4096 bytes of incrementing pattern.
        constexpr size_t N = 4096;
        std::vector<uint8_t> data(N);
        for (size_t i = 0; i < N; ++i)
            data[i] = static_cast<uint8_t>(i & 0xFF);

        {
            auto f = File::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            int n = f.write_all(data.data(), data.size());
            LOGOS_ASSERT(n == (int)N, "REACTOR-FILE-T01a",
                         "write_all returned {}, expected {}", n, N);
        }

        // Read it back.
        {
            auto f = File::open(path, O_RDONLY);
            read_back.resize(N);
            int n = f.read(read_back.data(), N);
            LOGOS_ASSERT(n == (int)N, "REACTOR-FILE-T01b",
                         "read returned {}, expected {}", n, N);
        }

        written = data;
    }, "file-wr");

    reactor.run();

    LOGOS_ASSERT(written == read_back, "REACTOR-FILE-T01c",
                 "Write/read mismatch");
    fs::remove(path);
    LOGOS_TRACE("reactor.file.write_read", "ok", "");
    std::println("  [ok] test_file_write_read");
}

// ---------------------------------------------------------------------------
// Test 2: sequential reads (multiple read calls advance offset)
// ---------------------------------------------------------------------------
static void test_file_sequential_reads() {
    LOGOS_TRACE("reactor.file.sequential", "start", "");
    const char* path = "/tmp/logos_reactor_test_seq.bin";

    Reactor reactor;
    std::vector<uint8_t> reconstructed;

    reactor.spawn([&] {
        // Write 1024 bytes.
        std::vector<uint8_t> data(1024);
        for (size_t i = 0; i < data.size(); ++i)
            data[i] = static_cast<uint8_t>(i % 251);  // prime, avoids trivial pattern
        {
            auto f = File::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            f.write_all(data.data(), data.size());
        }

        // Read in 128-byte chunks.
        {
            auto f = File::open(path, O_RDONLY);
            uint8_t chunk[128];
            while (true) {
                int n = f.read(chunk, sizeof(chunk));
                if (n <= 0) break;
                for (int i = 0; i < n; ++i)
                    reconstructed.push_back(chunk[i]);
            }
        }
    }, "seq-read");

    reactor.run();

    LOGOS_ASSERT(reconstructed.size() == 1024, "REACTOR-FILE-T02a",
                 "Expected 1024 bytes, got {}", reconstructed.size());
    for (size_t i = 0; i < reconstructed.size(); ++i)
        LOGOS_ASSERT(reconstructed[i] == static_cast<uint8_t>(i % 251),
                     "REACTOR-FILE-T02b",
                     "Byte {} mismatch: got {}, expected {}",
                     i, reconstructed[i], i % 251);
    fs::remove(path);
    LOGOS_TRACE("reactor.file.sequential", "ok", "");
    std::println("  [ok] test_file_sequential_reads");
}

// ---------------------------------------------------------------------------
// Test 3: concurrent file IO + compute fibers
// ---------------------------------------------------------------------------
static void test_file_concurrent() {
    LOGOS_TRACE("reactor.file.concurrent", "start", "");
    const char* pathA = "/tmp/logos_reactor_test_ca.bin";
    const char* pathB = "/tmp/logos_reactor_test_cb.bin";

    Reactor reactor;
    bool a_done = false, b_done = false;
    int  compute_count = 0;

    // Fiber A: writes file A.
    reactor.spawn([&] {
        std::string msg = "fiber A was here";
        auto f = File::open(pathA, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        f.write_all(msg.data(), msg.size());
        a_done = true;
    }, "fiberA");

    // Fiber B: writes file B.
    reactor.spawn([&] {
        std::string msg = "fiber B was here";
        auto f = File::open(pathB, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        f.write_all(msg.data(), msg.size());
        b_done = true;
    }, "fiberB");

    // Compute fiber: increments counter while IO fibers run.
    reactor.spawn([&] {
        for (int i = 0; i < 5; ++i) {
            ++compute_count;
            Scheduler::current()->yield();
        }
    }, "compute");

    reactor.run();

    LOGOS_ASSERT(a_done, "REACTOR-FILE-T03a", "Fiber A did not complete");
    LOGOS_ASSERT(b_done, "REACTOR-FILE-T03b", "Fiber B did not complete");
    LOGOS_ASSERT(compute_count == 5, "REACTOR-FILE-T03c",
                 "Compute fiber ran {} times, expected 5", compute_count);
    fs::remove(pathA);
    fs::remove(pathB);
    LOGOS_TRACE("reactor.file.concurrent", "ok", "");
    std::println("  [ok] test_file_concurrent");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::println("=== reactor file exerciser (Layer 3 — io_uring file IO) ===");
    test_file_write_read();
    test_file_sequential_reads();
    test_file_concurrent();
    std::println("=== all tests passed ===");
    return 0;
}
