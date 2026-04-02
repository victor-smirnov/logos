// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/reactor/reactor.hpp>
#include <logos/verification/assert.hpp>

#include <liburing.h>
#include <cstring>

namespace logos::reactor {

static thread_local Reactor* tl_current_reactor = nullptr;

Reactor* Reactor::current() noexcept { return tl_current_reactor; }

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------
Reactor::Reactor(unsigned ring_depth) {
    ring_ = new io_uring{};
    int rc = io_uring_queue_init(ring_depth, ring_, 0);
    LOGOS_ASSERT(rc == 0, "REACTOR-INIT-001",
                 "io_uring_queue_init failed: {}", strerror(-rc));
}

Reactor::~Reactor() {
    if (ring_) { io_uring_queue_exit(ring_); delete ring_; ring_ = nullptr; }
}

// ---------------------------------------------------------------------------
// spawn
// ---------------------------------------------------------------------------
Fiber* Reactor::spawn(std::move_only_function<void()> fn,
                      std::string_view name, size_t stack_size)
{
    return sched_.spawn(std::move(fn), name, stack_size);
}

// ---------------------------------------------------------------------------
// run
// ---------------------------------------------------------------------------
void Reactor::run() {
    LOGOS_ASSERT(!tl_current_reactor, "REACTOR-RUN-001",
                 "Reactor::run() on thread that already has a reactor");
    tl_current_reactor = this;
    Scheduler::install(&sched_);

    while (!stop_requested_) {
        while (sched_.step()) {}

        if (pending_io_ > 0) {
            int n = reap_completions(/*wait=*/true);
            LOGOS_ASSERT(n > 0, "REACTOR-RUN-002",
                         "io_uring wait returned no completions (pending={})", pending_io_);
            reap_completions(/*wait=*/false);
            continue;
        }
        break;
    }

    Scheduler::uninstall();
    tl_current_reactor = nullptr;
}

// ---------------------------------------------------------------------------
// submit_and_wait — the single place where fibers block on IO
// ---------------------------------------------------------------------------
int Reactor::submit_and_wait(io_uring_sqe* sqe) {
    Fiber* self = sched_.running_;
    LOGOS_ASSERT(self, "REACTOR-IO-001", "IO operation called outside a fiber");

    IoOp op{self, 0};
    io_uring_sqe_set_data(sqe, &op);

    int rc = io_uring_submit(ring_);
    LOGOS_ASSERT(rc >= 0, "REACTOR-IO-002", "io_uring_submit failed: {}", strerror(-rc));

    ++pending_io_;
    self->state_    = FiberState::Blocked;
    sched_.running_ = nullptr;
    fiber_switch(&self->regs_, &sched_.sched_regs_);
    self->state_ = FiberState::Running;

    return op.result;
}

// ---------------------------------------------------------------------------
// Timer
// ---------------------------------------------------------------------------
void Reactor::sleep_for(std::chrono::nanoseconds duration) {
    LOGOS_ASSERT(sched_.running_, "REACTOR-SLEEP-001", "sleep_for() outside a fiber");

    __kernel_timespec ts{};
    auto secs  = std::chrono::duration_cast<std::chrono::seconds>(duration);
    ts.tv_sec  = secs.count();
    ts.tv_nsec = (duration - secs).count();

    auto* sqe = io_uring_get_sqe(ring_);
    LOGOS_ASSERT(sqe, "REACTOR-SLEEP-002", "io_uring_get_sqe returned null");
    io_uring_prep_timeout(sqe, &ts, 0, 0);
    submit_and_wait(sqe);  // result -ETIME on success; ignored
}

// ---------------------------------------------------------------------------
// File IO
// ---------------------------------------------------------------------------
int Reactor::read(int fd, void* buf, size_t size, off_t offset) {
    auto* sqe = io_uring_get_sqe(ring_);
    LOGOS_ASSERT(sqe, "REACTOR-READ-001", "io_uring_get_sqe returned null");
    io_uring_prep_read(sqe, fd, buf, static_cast<unsigned>(size), offset);
    return submit_and_wait(sqe);
}

int Reactor::write(int fd, const void* buf, size_t size, off_t offset) {
    auto* sqe = io_uring_get_sqe(ring_);
    LOGOS_ASSERT(sqe, "REACTOR-WRITE-001", "io_uring_get_sqe returned null");
    io_uring_prep_write(sqe, fd, buf, static_cast<unsigned>(size), offset);
    return submit_and_wait(sqe);
}

// ---------------------------------------------------------------------------
// Stream socket IO
// ---------------------------------------------------------------------------
int Reactor::accept(int listen_fd, sockaddr* addr, socklen_t* addrlen) {
    auto* sqe = io_uring_get_sqe(ring_);
    LOGOS_ASSERT(sqe, "REACTOR-ACCEPT-001", "io_uring_get_sqe returned null");
    io_uring_prep_accept(sqe, listen_fd, addr, addrlen, 0);
    return submit_and_wait(sqe);
}

int Reactor::connect(int fd, const sockaddr* addr, socklen_t addrlen) {
    auto* sqe = io_uring_get_sqe(ring_);
    LOGOS_ASSERT(sqe, "REACTOR-CONNECT-001", "io_uring_get_sqe returned null");
    io_uring_prep_connect(sqe, fd, addr, addrlen);
    return submit_and_wait(sqe);
}

int Reactor::recv(int fd, void* buf, size_t size, int flags) {
    auto* sqe = io_uring_get_sqe(ring_);
    LOGOS_ASSERT(sqe, "REACTOR-RECV-001", "io_uring_get_sqe returned null");
    io_uring_prep_recv(sqe, fd, buf, size, flags);
    return submit_and_wait(sqe);
}

int Reactor::send(int fd, const void* buf, size_t size, int flags) {
    auto* sqe = io_uring_get_sqe(ring_);
    LOGOS_ASSERT(sqe, "REACTOR-SEND-001", "io_uring_get_sqe returned null");
    io_uring_prep_send(sqe, fd, buf, size, flags);
    return submit_and_wait(sqe);
}

// ---------------------------------------------------------------------------
// Message IO (UDP / QUIC)
// ---------------------------------------------------------------------------
int Reactor::sendmsg(int fd, const ::msghdr* msg, int flags) {
    auto* sqe = io_uring_get_sqe(ring_);
    LOGOS_ASSERT(sqe, "REACTOR-SENDMSG-001", "io_uring_get_sqe returned null");
    io_uring_prep_sendmsg(sqe, fd, msg, flags);
    return submit_and_wait(sqe);
}

int Reactor::recvmsg(int fd, ::msghdr* msg, int flags) {
    auto* sqe = io_uring_get_sqe(ring_);
    LOGOS_ASSERT(sqe, "REACTOR-RECVMSG-001", "io_uring_get_sqe returned null");
    io_uring_prep_recvmsg(sqe, fd, msg, flags);
    return submit_and_wait(sqe);
}

// ---------------------------------------------------------------------------
// Poll (for signalfd, eventfd, etc.)
// ---------------------------------------------------------------------------
int Reactor::poll_one(int fd, uint32_t poll_mask) {
    auto* sqe = io_uring_get_sqe(ring_);
    LOGOS_ASSERT(sqe, "REACTOR-POLL-001", "io_uring_get_sqe returned null");
    io_uring_prep_poll_add(sqe, fd, poll_mask);
    return submit_and_wait(sqe);
}

// ---------------------------------------------------------------------------
// reap_completions
// ---------------------------------------------------------------------------
int Reactor::reap_completions(bool wait) {
    int count = 0;

    auto process = [&](io_uring_cqe* cqe) {
        auto* op = static_cast<IoOp*>(io_uring_cqe_get_data(cqe));
        if (op && op->fiber) {
            op->result = cqe->res;
            sched_.wake(op->fiber);
            --pending_io_;
            ++count;
        }
        io_uring_cqe_seen(ring_, cqe);
    };

    if (wait) {
        io_uring_cqe* cqe = nullptr;
        if (io_uring_wait_cqe(ring_, &cqe) < 0) return 0;
        process(cqe);
    }

    io_uring_cqe* cqe = nullptr;
    unsigned head;
    io_uring_for_each_cqe(ring_, head, cqe) { process(cqe); }

    return count;
}

} // namespace logos::reactor
