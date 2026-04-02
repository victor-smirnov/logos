// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <logos/reactor/scheduler.hpp>

#include <chrono>
#include <sys/socket.h>  // sockaddr, socklen_t, msghdr — all socket primitives

struct io_uring;      // forward declare — avoid including liburing.h in public header
struct io_uring_sqe;  // submission queue entry

namespace logos::reactor {

// ---------------------------------------------------------------------------
// Reactor — one io_uring instance + Scheduler per OS thread.
//
// All IO primitives below may only be called from inside a fiber running on
// this reactor. They submit an io_uring SQE, suspend the calling fiber, and
// resume it when the kernel posts the CQE. The kernel result (>=0 bytes, or
// -errno) is returned directly.
//
// Typical usage:
//
//   Reactor reactor;
//   reactor.spawn([] {
//       auto* r = Reactor::current();
//       // file IO
//       int fd = ::open("f.txt", O_RDONLY);
//       char buf[256];
//       int n = r->read(fd, buf, sizeof(buf));
//       // TCP: see TcpSocket
//       // UDP: see UdpSocket
//       // UDS: see UnixSocket
//       // Signals: see SignalWatcher
//   });
//   reactor.run();
// ---------------------------------------------------------------------------
class Reactor {
public:
    static constexpr unsigned kRingDepth = 256;

    explicit Reactor(unsigned ring_depth = kRingDepth);
    ~Reactor();

    Reactor(const Reactor&)            = delete;
    Reactor& operator=(const Reactor&) = delete;

    Fiber* spawn(std::move_only_function<void()> fn,
                 std::string_view                name       = "",
                 size_t                          stack_size = Fiber::kDefaultStackSize);

    // Run until all fibers complete, or stop() is called.
    void run();

    // Request exit at next event loop iteration.
    void stop() noexcept { stop_requested_ = true; }
    bool stop_requested() const noexcept { return stop_requested_; }

    // -------------------------------------------------------------------------
    // Timer
    // -------------------------------------------------------------------------

    void sleep_for(std::chrono::nanoseconds duration);

    template <class Rep, class Period>
    void sleep_for(std::chrono::duration<Rep, Period> d) {
        sleep_for(std::chrono::duration_cast<std::chrono::nanoseconds>(d));
    }

    // -------------------------------------------------------------------------
    // File IO  (offset == -1 → current file position)
    // -------------------------------------------------------------------------

    int read (int fd, void*       buf, size_t size, off_t offset = -1);
    int write(int fd, const void* buf, size_t size, off_t offset = -1);

    // -------------------------------------------------------------------------
    // Stream socket IO (TCP / UDS-stream)
    // -------------------------------------------------------------------------

    int accept (int listen_fd,
                sockaddr*  addr    = nullptr,
                socklen_t* addrlen = nullptr);
    int connect(int fd, const sockaddr* addr, socklen_t addrlen);
    int recv   (int fd, void*       buf, size_t size, int flags = 0);
    int send   (int fd, const void* buf, size_t size, int flags = 0);

    // -------------------------------------------------------------------------
    // Message IO (UDP / QUIC — sendmsg/recvmsg with full msghdr control)
    // -------------------------------------------------------------------------

    int sendmsg(int fd, const ::msghdr* msg, int flags = 0);
    int recvmsg(int fd,       ::msghdr* msg, int flags = 0);

    // -------------------------------------------------------------------------
    // Poll — wait for a single readiness event on fd (e.g. signalfd).
    // poll_mask: POLLIN, POLLOUT, etc.  Returns revents mask, or -errno.
    // -------------------------------------------------------------------------

    int poll_one(int fd, uint32_t poll_mask);

    // -------------------------------------------------------------------------
    // Accessors
    // -------------------------------------------------------------------------

    static Reactor* current() noexcept;

    Scheduler& scheduler() noexcept { return sched_; }

private:
    struct IoOp { Fiber* fiber; int result = 0; };

    int submit_and_wait(io_uring_sqe* sqe);
    int reap_completions(bool wait);

    Scheduler  sched_;
    io_uring*  ring_;
    int        pending_io_     = 0;
    bool       stop_requested_ = false;
};

} // namespace logos::reactor
