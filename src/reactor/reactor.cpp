// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/reactor/reactor.hpp>
#include <logos/reactor/reactor_engine.hpp>
#include <logos/verification/assert.hpp>

#include <liburing.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <cstring>

namespace logos::reactor {

static thread_local Reactor* tl_current_reactor = nullptr;

Reactor* Reactor::current() noexcept { return tl_current_reactor; }

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------
Reactor::Reactor(unsigned ring_depth, size_t stack_size) noexcept
    : sched_(stack_size)
{
    ring_ = new io_uring{};
    int rc = io_uring_queue_init(ring_depth, ring_, 0);
    LOGOS_ASSERT(rc == 0, "REACTOR-INIT-001",
                 "io_uring_queue_init failed: {}", strerror(-rc));

    alien_efd_ = ::eventfd(0, EFD_CLOEXEC);
    LOGOS_ASSERT(alien_efd_ >= 0, "REACTOR-INIT-002",
                 "eventfd() for alien queue failed: {}", strerror(errno));
}

Reactor::~Reactor() noexcept {
    if (alien_efd_ >= 0) { ::close(alien_efd_); alien_efd_ = -1; }
    if (ring_) { io_uring_queue_exit(ring_); delete ring_; ring_ = nullptr; }
}

// ---------------------------------------------------------------------------
// spawn
// ---------------------------------------------------------------------------
Fiber* Reactor::spawn(std::move_only_function<void()> fn,
                      std::string_view name, size_t stack_size) noexcept
{
    return sched_.spawn(std::move(fn), name, stack_size);
}

// ---------------------------------------------------------------------------
// run
// ---------------------------------------------------------------------------
void Reactor::run() noexcept {
    LOGOS_ASSERT(!tl_current_reactor, "REACTOR-RUN-001",
                 "Reactor::run() on thread that already has a reactor");
    tl_current_reactor = this;
    Scheduler::install(&sched_);

    rearm_alien_();  // start listening for alien task submissions

    while (!stop_requested_) {
        drain_p2p_();
        drain_alien_();
        while (sched_.step()) {}

        if (pending_io_ > 0) {
            sleeping_.store(true, std::memory_order_release);
            reap_completions(/*wait=*/true);
            sleeping_.store(false, std::memory_order_relaxed);
            reap_completions(/*wait=*/false);
            continue;  // alien wakeup or real IO — either way, loop back
        }
        break;
    }

    Scheduler::uninstall();
    tl_current_reactor = nullptr;
}

// ---------------------------------------------------------------------------
// submit_and_wait — the single place where fibers block on IO
// ---------------------------------------------------------------------------
logos::expected<int> Reactor::submit_and_wait(io_uring_sqe* sqe) noexcept {
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

    if (op.result < 0)
        return std::unexpected(logos::err(ErrCode::io_error));
    return op.result;
}

// ---------------------------------------------------------------------------
// Timer
// ---------------------------------------------------------------------------
void Reactor::sleep_for(std::chrono::nanoseconds duration) noexcept {
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
logos::expected<int> Reactor::read(int fd, void* buf, size_t size, off_t offset) noexcept {
    auto* sqe = io_uring_get_sqe(ring_);
    LOGOS_ASSERT(sqe, "REACTOR-READ-001", "io_uring_get_sqe returned null");
    io_uring_prep_read(sqe, fd, buf, static_cast<unsigned>(size), offset);
    return submit_and_wait(sqe);
}

logos::expected<int> Reactor::write(int fd, const void* buf, size_t size, off_t offset) noexcept {
    auto* sqe = io_uring_get_sqe(ring_);
    LOGOS_ASSERT(sqe, "REACTOR-WRITE-001", "io_uring_get_sqe returned null");
    io_uring_prep_write(sqe, fd, buf, static_cast<unsigned>(size), offset);
    return submit_and_wait(sqe);
}

// ---------------------------------------------------------------------------
// Stream socket IO
// ---------------------------------------------------------------------------
logos::expected<int> Reactor::accept(int listen_fd, sockaddr* addr, socklen_t* addrlen) noexcept {
    auto* sqe = io_uring_get_sqe(ring_);
    LOGOS_ASSERT(sqe, "REACTOR-ACCEPT-001", "io_uring_get_sqe returned null");
    io_uring_prep_accept(sqe, listen_fd, addr, addrlen, 0);
    return submit_and_wait(sqe);
}

logos::expected<int> Reactor::connect(int fd, const sockaddr* addr, socklen_t addrlen) noexcept {
    auto* sqe = io_uring_get_sqe(ring_);
    LOGOS_ASSERT(sqe, "REACTOR-CONNECT-001", "io_uring_get_sqe returned null");
    io_uring_prep_connect(sqe, fd, addr, addrlen);
    return submit_and_wait(sqe);
}

logos::expected<int> Reactor::recv(int fd, void* buf, size_t size, int flags) noexcept {
    auto* sqe = io_uring_get_sqe(ring_);
    LOGOS_ASSERT(sqe, "REACTOR-RECV-001", "io_uring_get_sqe returned null");
    io_uring_prep_recv(sqe, fd, buf, size, flags);
    return submit_and_wait(sqe);
}

logos::expected<int> Reactor::send(int fd, const void* buf, size_t size, int flags) noexcept {
    auto* sqe = io_uring_get_sqe(ring_);
    LOGOS_ASSERT(sqe, "REACTOR-SEND-001", "io_uring_get_sqe returned null");
    io_uring_prep_send(sqe, fd, buf, size, flags);
    return submit_and_wait(sqe);
}

// ---------------------------------------------------------------------------
// Message IO (UDP / QUIC)
// ---------------------------------------------------------------------------
logos::expected<int> Reactor::sendmsg(int fd, const ::msghdr* msg, int flags) noexcept {
    auto* sqe = io_uring_get_sqe(ring_);
    LOGOS_ASSERT(sqe, "REACTOR-SENDMSG-001", "io_uring_get_sqe returned null");
    io_uring_prep_sendmsg(sqe, fd, msg, flags);
    return submit_and_wait(sqe);
}

logos::expected<int> Reactor::recvmsg(int fd, ::msghdr* msg, int flags) noexcept {
    auto* sqe = io_uring_get_sqe(ring_);
    LOGOS_ASSERT(sqe, "REACTOR-RECVMSG-001", "io_uring_get_sqe returned null");
    io_uring_prep_recvmsg(sqe, fd, msg, flags);
    return submit_and_wait(sqe);
}

// ---------------------------------------------------------------------------
// Poll (for signalfd, eventfd, etc.)
// ---------------------------------------------------------------------------
logos::expected<int> Reactor::poll_one(int fd, uint32_t poll_mask) noexcept {
    auto* sqe = io_uring_get_sqe(ring_);
    LOGOS_ASSERT(sqe, "REACTOR-POLL-001", "io_uring_get_sqe returned null");
    io_uring_prep_poll_add(sqe, fd, poll_mask);
    return submit_and_wait(sqe);
}

// ---------------------------------------------------------------------------
// reap_completions
// ---------------------------------------------------------------------------
int Reactor::reap_completions(bool wait) noexcept {
    int count = 0;

    auto process = [&](io_uring_cqe* cqe) noexcept {
        auto* op = static_cast<IoOp*>(io_uring_cqe_get_data(cqe));
        if (!op) { io_uring_cqe_seen(ring_, cqe); return; }

        if (op->fiber) {
            // Normal IO completion — wake the blocked fiber.
            op->result = cqe->res;
            sched_.wake(op->fiber);
            --pending_io_;
            ++count;
        } else {
            // Alien wake-up — drain the queue and re-arm the read.
            if (cqe->res > 0) {
                drain_alien_();
                rearm_alien_();
            }
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

// ---------------------------------------------------------------------------
// Alien task queue
// ---------------------------------------------------------------------------

void Reactor::alien_submit(std::move_only_function<void()> fn) noexcept {
    // Fast path: reactor-to-reactor via P2P SPSC queue (lock-free).
    Reactor* caller = Reactor::current();
    if (caller && engine_ && caller->engine_ == engine_) {
        auto& q = engine_->queue(caller->id_, id_);
        bool ok = q.push(std::move(fn));
        LOGOS_ASSERT(ok, "REACTOR-ALIEN-003",
                     "P2P SPSC queue full (reactor {} -> {})", caller->id_, id_);
        // Conditional wakeup: only write eventfd if target is sleeping.
        if (sleeping_.load(std::memory_order_acquire)) {
            uint64_t one = 1;
            ::write(alien_efd_, &one, sizeof(one));
        }
        return;
    }

    // Slow path: non-reactor thread — mutex-protected queue.
    {
        std::lock_guard lock(alien_mutex_);
        alien_pending_.push_back(std::move(fn));
    }
    uint64_t one = 1;
    ::write(alien_efd_, &one, sizeof(one));  // always wake — can't check sleeping_ safely
}

void Reactor::drain_p2p_() noexcept {
    if (!engine_) return;  // standalone reactor — no P2P queues

    size_t n = engine_->size();
    for (size_t src = 0; src < n; ++src) {
        auto& q = engine_->queue(src, id_);
        std::move_only_function<void()> task;
        while (q.pop(task))
            sched_.spawn(std::move(task));
    }
}

void Reactor::drain_alien_() noexcept {
    std::vector<std::move_only_function<void()>> tasks;
    {
        std::lock_guard lock(alien_mutex_);
        tasks.swap(alien_pending_);
    }
    for (auto& fn : tasks)
        sched_.spawn(std::move(fn));
}

void Reactor::rearm_alien_() noexcept {
    auto* sqe = io_uring_get_sqe(ring_);
    LOGOS_ASSERT(sqe, "REACTOR-ALIEN-001", "io_uring_get_sqe returned null");
    io_uring_prep_read(sqe, alien_efd_, &alien_buf_, sizeof(alien_buf_), 0);
    io_uring_sqe_set_data(sqe, &alien_op_);  // alien_op_.fiber == nullptr → sentinel
    int rc = io_uring_submit(ring_);
    LOGOS_ASSERT(rc >= 0, "REACTOR-ALIEN-002", "io_uring_submit failed: {}", strerror(-rc));
}

} // namespace logos::reactor
