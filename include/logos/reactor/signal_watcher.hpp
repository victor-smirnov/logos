// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// SignalWatcher — fiber-aware signal handling via signalfd + io_uring read.
//
// Blocks the nominated signals from normal delivery, creates a signalfd, and
// uses io_uring async read to wait for signal arrivals without busy-looping.
//
// Usage — graceful shutdown:
//
//   Reactor reactor;
//   SignalWatcher sig({SIGINT, SIGTERM});
//
//   reactor.spawn([&] {
//       int signo = sig.wait();   // blocks fiber until SIGINT or SIGTERM
//       std::println("Caught signal {}, stopping...", signo);
//       reactor.stop();
//   }, "signal-handler");
//
//   reactor.spawn([] { /* ... main work ... */ });
//   reactor.run();
//
// Notes:
// - Signals must be blocked in the calling thread before creating SignalWatcher
//   (the constructor does this automatically).
// - wait() can be called multiple times to receive successive signals.
// - Only one fiber should call wait() at a time on a given watcher.

#pragma once

#include <logos/reactor/reactor.hpp>
#include <logos/verification/assert.hpp>

#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <initializer_list>
#include <stdexcept>
#include <string>
#include <cstring>

namespace logos::reactor {

class SignalWatcher {
public:
    // Block 'signals' from normal delivery and create a signalfd for them.
    explicit SignalWatcher(std::initializer_list<int> signals) {
        sigemptyset(&mask_);
        for (int sig : signals)
            sigaddset(&mask_, sig);

        // Block these signals on the calling thread.
        if (sigprocmask(SIG_BLOCK, &mask_, &saved_mask_) < 0)
            throw std::runtime_error(std::string("sigprocmask: ") + strerror(errno));

        sfd_ = signalfd(-1, &mask_, SFD_CLOEXEC);
        if (sfd_ < 0) {
            sigprocmask(SIG_SETMASK, &saved_mask_, nullptr);
            throw std::runtime_error(std::string("signalfd: ") + strerror(errno));
        }
    }

    ~SignalWatcher() {
        if (sfd_ >= 0) {
            ::close(sfd_);
            sigprocmask(SIG_SETMASK, &saved_mask_, nullptr);
        }
    }

    SignalWatcher(const SignalWatcher&)            = delete;
    SignalWatcher& operator=(const SignalWatcher&) = delete;

    // Block the calling fiber until one of the watched signals arrives.
    // Returns the signal number.
    int wait() {
        Reactor* r = Reactor::current();
        LOGOS_ASSERT(r, "REACTOR-SIG-001", "SignalWatcher::wait() called outside reactor");

        // io_uring async read: blocks fiber until a signal is pending, then
        // delivers the signalfd_siginfo atomically (no separate poll needed).
        signalfd_siginfo info{};
        int n = r->read(sfd_, &info, sizeof(info));
        LOGOS_ASSERT(n == (int)sizeof(info), "REACTOR-SIG-002",
                     "signalfd read returned {}, expected {}", n, (int)sizeof(info));

        return static_cast<int>(info.ssi_signo);
    }

    int fd() const noexcept { return sfd_; }

private:
    sigset_t mask_{};
    sigset_t saved_mask_{};
    int      sfd_ = -1;
};

} // namespace logos::reactor
