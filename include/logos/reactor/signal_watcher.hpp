// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// SignalWatcher — fiber-aware signal handling via signalfd + io_uring read.
//
// Usage — graceful shutdown:
//
//   auto sig = logos::make_object<SignalWatcher>({SIGINT, SIGTERM}).get();
//
//   reactor.spawn([&] {
//       int signo = sig.wait();
//       std::println("Caught signal {}, stopping...", signo);
//       reactor.stop();
//   }, "signal-handler");
//
// Notes:
// - Signals are blocked on the calling thread by the constructor.
// - wait() can be called multiple times to receive successive signals.
// - Only one fiber should call wait() at a time on a given watcher.

#pragma once

#include <logos/reactor/reactor.hpp>
#include <logos/verification/assert.hpp>
#include <logos/core/make_object.hpp>

#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <initializer_list>

namespace logos::reactor {

class SignalWatcher {
public:
    // Fallible constructor — use logos::make_object<SignalWatcher>(signals).
    SignalWatcher(logos::InitTag& tag,
                  std::initializer_list<int> signals) noexcept {
        sigemptyset(&mask_);
        for (int sig : signals) sigaddset(&mask_, sig);

        if (sigprocmask(SIG_BLOCK, &mask_, &saved_mask_) < 0) {
            tag.fail(logos::err(ErrCode::sigprocmask_error));
            return;
        }
        sfd_ = signalfd(-1, &mask_, SFD_CLOEXEC);
        if (sfd_ < 0) {
            sigprocmask(SIG_SETMASK, &saved_mask_, nullptr);
            tag.fail(logos::err(ErrCode::signalfd_error));
        }
    }

    ~SignalWatcher() {
        if (sfd_ >= 0) {
            ::close(sfd_);
            sigprocmask(SIG_SETMASK, &saved_mask_, nullptr);
        }
    }

    SignalWatcher(SignalWatcher&& o) noexcept
        : mask_(o.mask_), saved_mask_(o.saved_mask_), sfd_(o.sfd_)
    {
        o.sfd_ = -1;
    }

    SignalWatcher& operator=(SignalWatcher&& o) noexcept {
        if (this != &o) {
            if (sfd_ >= 0) { ::close(sfd_); sigprocmask(SIG_SETMASK, &saved_mask_, nullptr); }
            mask_       = o.mask_;
            saved_mask_ = o.saved_mask_;
            sfd_        = o.sfd_;
            o.sfd_      = -1;
        }
        return *this;
    }

    SignalWatcher(const SignalWatcher&)            = delete;
    SignalWatcher& operator=(const SignalWatcher&) = delete;

    // Block the calling fiber until one of the watched signals arrives.
    // Returns the signal number.
    int wait() {
        Reactor* r = Reactor::current();
        LOGOS_ASSERT(r, "REACTOR-SIG-001", "SignalWatcher::wait() called outside reactor");

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
