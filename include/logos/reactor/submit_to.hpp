//
// submit_to — Seastar-style cross-reactor work submission.
//
// Posts a callable to run as a new fiber on a target Reactor (which may be
// on a different OS thread), suspends the calling fiber, and returns the
// result when the target fiber completes.
//
// fn must return logos::expected<T> (or logos::expected<void>).
// submit_to returns the same type as fn.
//
// Usage:
//
//   // Reactor A and B each run on their own std::thread.
//   // From a fiber on reactor_a:
//
//   auto result = submit_to(reactor_b, []() noexcept -> logos::expected<int> {
//       // This body runs as a new fiber on reactor_b.
//       // May call reactor_b IO methods, sleep_for, etc.
//       return 42;
//   });
//   if (!result) { /* handle error */ }
//   int v = *result;
//
// Notes:
// - The calling fiber is suspended for the duration of fn() on target.
// - fn() runs with reactor_b as Reactor::current().
// - Errors from fn() propagate through the expected return value.
// - submit_to itself may return ErrCode::eventfd_error on eventfd() failure.
//
// Cross-reactor ownership rules:
// - Lambda captures must be by move only — no & captures of reactor-local state.
// - After move, the source reactor must not access the moved object.
// - Own<Hermes> may be moved across reactors (atomic refcount in MemHolder).
// - Sealed arenas (Arena::seal()) may be shared read-only across reactors.
// - POD types are always safe to send.
// - The return value is moved back to the calling reactor.

#pragma once

#include <logos/reactor/reactor.hpp>
#include <logos/verification/assert.hpp>

#include <sys/eventfd.h>
#include <unistd.h>

#include <memory>
#include <utility>

namespace logos::reactor {

template<typename Fn,
         typename Ret = std::invoke_result_t<Fn>>
[[nodiscard]]
Ret submit_to(Reactor& target, Fn fn) noexcept {
    Reactor* self = Reactor::current();
    LOGOS_ASSERT(self, "REACTOR-SUBMIT-001",
                 "submit_to() called outside a reactor fiber");

    // One-shot shared state: stores fn's result and the notification eventfd.
    struct State {
        Ret      result;
        int      efd = -1;
        ~State() noexcept { if (efd >= 0) { ::close(efd); efd = -1; } }
    };

    auto state = std::make_shared<State>();
    state->efd = ::eventfd(0, EFD_CLOEXEC);
    if (state->efd < 0)
        return std::unexpected(logos::err(ErrCode::eventfd_error));

    // Post work to target reactor.  Runs as a new fiber; signals state->efd
    // when fn() completes so the caller's io_uring read unblocks.
    target.alien_submit([state, fn = std::move(fn)]() mutable noexcept {
        state->result = fn();
        uint64_t one = 1;
        ::write(state->efd, &one, sizeof(one));  // never blocks; result ignored
    });

    // Suspend calling fiber until target signals completion.
    uint64_t v;
    LOGOS_TRY(auto n, self->read(state->efd, &v, sizeof(v)));
    (void)n;

    return std::move(state->result);
}

} // namespace logos::reactor
