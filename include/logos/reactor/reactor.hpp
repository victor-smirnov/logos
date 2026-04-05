// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <logos/reactor/scheduler.hpp>
#include <logos/core/expected.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <vector>
#include <sys/socket.h>  // sockaddr, socklen_t, msghdr — all socket primitives

struct io_uring;      // forward declare — avoid including liburing.h in public header
struct io_uring_sqe;  // submission queue entry

LOGOS_NS_BEGIN

// ---------------------------------------------------------------------------
// Reactor scalar error codes (range 0x0003'0000 … 0x0003'FFFF).
// ---------------------------------------------------------------------------
enum class ErrCode : uint64_t {
    socket_error      = 0x0003'0001,
    bind_error        = 0x0003'0002,
    listen_error      = 0x0003'0003,
    connect_error     = 0x0003'0004,
    accept_error      = 0x0003'0005,
    open_error        = 0x0003'0006,
    sigprocmask_error = 0x0003'0007,
    signalfd_error    = 0x0003'0008,
    invalid_host      = 0x0003'0009,
    io_error          = 0x0003'000A,
    channel_full      = 0x0003'000B,
    eventfd_error     = 0x0003'000C,
};

class ReactorEngine;  // forward declare

// ---------------------------------------------------------------------------
// Reactor — one io_uring instance + Scheduler per OS thread.
//
// All IO primitives below may only be called from inside a fiber running on
// this reactor. They submit an io_uring SQE, suspend the calling fiber, and
// resume it when the kernel posts the CQE. The kernel result (>=0 bytes, or
// -errno) is returned directly.
//
// Cross-reactor calls — submit_to():
//   Each Reactor has an alien task queue.  Tasks posted from other threads
//   via alien_submit() are picked up by the event loop and run as new fibers.
//   Use submit_to() (see submit_to.hpp) to post work and await the result:
//
//   // From a fiber on reactor_a:
//   int v = submit_to(reactor_b, []() noexcept -> logos::expected<int> {
//       // runs as a new fiber on reactor_b
//       return 42;
//   }).get();
//
// Typical usage:
//
//   Reactor reactor;
//   reactor.spawn([] {
//       auto* r = Reactor::current();
//       char buf[256];
//       int n = r->read(fd, buf, sizeof(buf));
//   });
//   reactor.run();
// ---------------------------------------------------------------------------
class Reactor {
public:
    static constexpr unsigned kRingDepth = 256;

    // Red: run in scheduler/thread context (not on a fiber stack).
    LOGOS_RED explicit Reactor(unsigned ring_depth = kRingDepth,
                               size_t   stack_size = Fiber::kDefaultStackSize) noexcept;
    LOGOS_RED ~Reactor() noexcept;

    Reactor(const Reactor&)            = delete;
    Reactor& operator=(const Reactor&) = delete;

    // Red: called before entering the scheduler loop.
#if LOGOS_HAS_GREEN_STACKS
    LOGOS_RED Fiber* spawn(GreenFn fn,
                           std::string_view name       = "",
                           size_t           stack_size = Fiber::kDefaultStackSize) noexcept;
#else
    Fiber* spawn(std::move_only_function<void()> fn,
                 std::string_view                name       = "",
                 size_t                          stack_size = Fiber::kDefaultStackSize) noexcept;
#endif

    // Red: scheduler event loop.
    LOGOS_RED void run() noexcept;
    LOGOS_RED void stop() noexcept { stop_requested_ = true; }
    LOGOS_RED bool stop_requested() const noexcept { return stop_requested_; }

    // -------------------------------------------------------------------------
    // Fiber-callable API — green (inherited from namespace LOGOS_GREEN).
    //
    // These suspend the calling fiber via fiber_switch and must run on the
    // fiber's green stack.  Red sub-calls (io_uring, liburing) auto-switch to
    // the system thread stack via Jenny.
    // -------------------------------------------------------------------------

    // Timer
    void sleep_for(std::chrono::nanoseconds duration) noexcept;

    template <class Rep, class Period>
    void sleep_for(std::chrono::duration<Rep, Period> d) noexcept {
        sleep_for(std::chrono::duration_cast<std::chrono::nanoseconds>(d));
    }

    // File IO  (offset == -1 → current file position)
    logos::expected<int> read (int fd, void*       buf, size_t size, off_t offset = -1) noexcept;
    logos::expected<int> write(int fd, const void* buf, size_t size, off_t offset = -1) noexcept;

    // Stream socket IO (TCP / UDS-stream)
    logos::expected<int> accept (int listen_fd,
                                 sockaddr*  addr    = nullptr,
                                 socklen_t* addrlen = nullptr) noexcept;
    logos::expected<int> connect(int fd, const sockaddr* addr, socklen_t addrlen) noexcept;
    logos::expected<int> recv   (int fd, void*       buf, size_t size, int flags = 0) noexcept;
    logos::expected<int> send   (int fd, const void* buf, size_t size, int flags = 0) noexcept;

    // Message IO (UDP / QUIC)
    logos::expected<int> sendmsg(int fd, const ::msghdr* msg, int flags = 0) noexcept;
    logos::expected<int> recvmsg(int fd,       ::msghdr* msg, int flags = 0) noexcept;

    // Poll — wait for a single readiness event on fd (e.g. signalfd).
    logos::expected<int> poll_one(int fd, uint32_t poll_mask) noexcept;

    // -------------------------------------------------------------------------
    // Alien task queue — cross-reactor work submission (red: called off-fiber).
    // -------------------------------------------------------------------------
#if LOGOS_HAS_GREEN_STACKS
    LOGOS_RED void alien_submit(GreenFn fn) noexcept;
#else
    void alien_submit(std::move_only_function<void()> fn) noexcept;
#endif

    // -------------------------------------------------------------------------
    // Accessors — red: called from scheduler/thread context.
    // -------------------------------------------------------------------------
    LOGOS_RED static Reactor* current() noexcept;
    LOGOS_RED size_t         id()      const noexcept { return id_; }
    LOGOS_RED ReactorEngine* engine()  const noexcept { return engine_; }
    LOGOS_RED Scheduler&     scheduler()     noexcept { return sched_; }

private:
    friend class ReactorEngine;

    struct IoOp { Fiber* fiber; int result = 0; };

    // Green: called from fiber context, saves green RSP via fiber_switch.
    logos::expected<int> submit_and_wait(io_uring_sqe* sqe) noexcept;

    // Red: scheduler event-loop helpers.
    LOGOS_RED int  reap_completions(bool wait) noexcept;
    LOGOS_RED void drain_p2p_() noexcept;
    LOGOS_RED void drain_alien_() noexcept;
    LOGOS_RED void rearm_alien_() noexcept;

    Scheduler  sched_;
    io_uring*  ring_;
    int        pending_io_     = 0;
    bool       stop_requested_ = false;

    // Engine membership (set by ReactorEngine; default = standalone).
    size_t         id_     = 0;
    ReactorEngine* engine_ = nullptr;

    // Conditional wakeup: true while blocked in io_uring_wait_cqe.
    // Writers check this to decide whether to write to alien_efd_.
    std::atomic<bool> sleeping_{false};

    // Alien queue — tasks submitted from non-reactor OS threads.
    std::mutex alien_mutex_;
#if LOGOS_HAS_GREEN_STACKS
    std::vector<GreenFn> alien_pending_;
#else
    std::vector<std::move_only_function<void()>> alien_pending_;
#endif
    uint64_t                                     alien_buf_  = 0;
    int                                          alien_efd_  = -1;
    IoOp                                         alien_op_{};  // sentinel: fiber == nullptr
};

LOGOS_NS_END
