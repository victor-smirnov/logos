//
// Cross-reactor SPSC channel — fiber-aware message passing between two
// Reactor instances running on different OS threads.
//
// Backed by SpscQueue<T,N> for the data path and an EFD_SEMAPHORE eventfd
// for blocking/waking the consumer fiber via io_uring.
//
// One token is added to the eventfd per pushed item; the consumer always
// reads exactly one token before popping, guaranteeing 1:1 correspondence
// with no lost wakeups or spurious queue-empty assertions.
//
// Usage:
//
//   // Before starting reactors (any thread):
//   auto [tx, rx] = make_cross_channel<int, 256>().get();
//
//   // Reactor A (producer fiber):
//   tx.send(42).get();
//
//   // Reactor B (consumer fiber):
//   int v = rx.recv().get();   // suspends until message arrives
//
// Notes:
// - CrossSender::send() is a direct ::write() to eventfd — never blocks.
// - CrossReceiver::recv() suspends the calling fiber via io_uring read.
// - The queue is full → send() returns ErrCode::channel_full.
// - Move-only: each end must be owned by exactly one fiber/reactor.

#pragma once

#include <logos/reactor/reactor.hpp>
#include <logos/reactor/spsc_queue.hpp>
#include <logos/verification/assert.hpp>

#include <sys/eventfd.h>
#include <unistd.h>

#include <memory>
#include <utility>

namespace logos::reactor {

// ---------------------------------------------------------------------------
// Forward declarations (needed for friend declarations below).
// ---------------------------------------------------------------------------
template<typename T, size_t N> class CrossSender;
template<typename T, size_t N> class CrossReceiver;

template<typename T, size_t N>
logos::expected<std::pair<CrossSender<T,N>, CrossReceiver<T,N>>>
make_cross_channel() noexcept;

// ---------------------------------------------------------------------------
// Shared state — owned jointly by CrossSender and CrossReceiver via
// shared_ptr.  Created once before the reactors start.
// ---------------------------------------------------------------------------
namespace detail {

template<typename T, size_t N>
struct CrossState {
    SpscQueue<T, N> queue;
    int             efd = -1;

    CrossState() = default;
    ~CrossState() noexcept { if (efd >= 0) ::close(efd); }

    CrossState(const CrossState&)            = delete;
    CrossState& operator=(const CrossState&) = delete;
};

} // namespace detail

// ---------------------------------------------------------------------------
// CrossSender — producer end, used from the producer reactor's fiber.
// ---------------------------------------------------------------------------
template<typename T, size_t N = 256>
class CrossSender {
public:
    CrossSender()  = default;
    ~CrossSender() = default;

    CrossSender(CrossSender&&) noexcept            = default;
    CrossSender& operator=(CrossSender&&) noexcept = default;

    CrossSender(const CrossSender&)            = delete;
    CrossSender& operator=(const CrossSender&) = delete;

    // Push one item and signal the consumer.
    // Returns ErrCode::channel_full if the SPSC queue is full.
    // The ::write() to eventfd never blocks (EFD_SEMAPHORE is O(1)).
    [[nodiscard]]
    logos::expected<void> send(T val) noexcept {
        if (!state_->queue.push(std::move(val)))
            return std::unexpected(logos::err(ErrCode::channel_full));
        uint64_t one = 1;
        ::write(state_->efd, &one, sizeof(one));  // non-blocking; result ignored
        return {};
    }

    bool valid() const noexcept { return state_ != nullptr; }

private:
    template<typename T2, size_t N2>
    friend logos::expected<std::pair<CrossSender<T2,N2>, CrossReceiver<T2,N2>>>
        make_cross_channel() noexcept;

    explicit CrossSender(std::shared_ptr<detail::CrossState<T,N>> s) noexcept
        : state_(std::move(s)) {}

    std::shared_ptr<detail::CrossState<T,N>> state_;
};

// ---------------------------------------------------------------------------
// CrossReceiver — consumer end, used from the consumer reactor's fiber.
// ---------------------------------------------------------------------------
template<typename T, size_t N = 256>
class CrossReceiver {
public:
    CrossReceiver()  = default;
    ~CrossReceiver() = default;

    CrossReceiver(CrossReceiver&&) noexcept            = default;
    CrossReceiver& operator=(CrossReceiver&&) noexcept = default;

    CrossReceiver(const CrossReceiver&)            = delete;
    CrossReceiver& operator=(const CrossReceiver&) = delete;

    // Block the calling fiber until one message is available, then return it.
    // Uses io_uring to read one semaphore token from eventfd — the fiber
    // suspends if no tokens are available and resumes when the producer sends.
    [[nodiscard]]
    logos::expected<T> recv() noexcept {
        uint64_t token;
        LOGOS_TRY(auto n, Reactor::current()->read(state_->efd, &token, sizeof(token)));
        LOGOS_ASSERT(n == (int)sizeof(token), "REACTOR-XCHAN-001",
                     "eventfd read returned {}, expected {}", n, (int)sizeof(token));
        T val;
        bool ok = state_->queue.pop(val);
        LOGOS_ASSERT(ok, "REACTOR-XCHAN-002",
                     "semaphore token consumed but SPSC queue is empty");
        return val;
    }

    bool valid() const noexcept { return state_ != nullptr; }

private:
    template<typename T2, size_t N2>
    friend logos::expected<std::pair<CrossSender<T2,N2>, CrossReceiver<T2,N2>>>
        make_cross_channel() noexcept;

    explicit CrossReceiver(std::shared_ptr<detail::CrossState<T,N>> s) noexcept
        : state_(std::move(s)) {}

    std::shared_ptr<detail::CrossState<T,N>> state_;
};

// ---------------------------------------------------------------------------
// Factory — creates a matched sender/receiver pair.
// Call before starting the reactors (from any thread).
// ---------------------------------------------------------------------------
template<typename T, size_t N>
logos::expected<std::pair<CrossSender<T,N>, CrossReceiver<T,N>>>
make_cross_channel() noexcept {
    auto state = std::make_shared<detail::CrossState<T,N>>();
    state->efd = ::eventfd(0, EFD_CLOEXEC | EFD_SEMAPHORE);
    if (state->efd < 0)
        return std::unexpected(logos::err(ErrCode::eventfd_error));
    return std::pair{
        CrossSender<T,N>{state},
        CrossReceiver<T,N>{state}
    };
}

} // namespace logos::reactor
