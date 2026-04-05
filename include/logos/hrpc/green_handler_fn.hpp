// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// GreenHandlerFn — a copyable, type-erased callable for HRPC handler functions.
//
// Replaces std::function<Response(Context&)> for handler storage so that
// handler bodies can call green (fiber-blocking) reactor primitives such as
// ctx.pop(), ctx.push(), and session.call().
//
// Design mirrors GreenFn (green_fn.hpp) but:
//   - Signature is Response(Context&) rather than void()
//   - Must be copyable (handlers are copied when spawning handler fibers)
//   - operator() is explicitly LOGOS_GREEN so the handler body runs on the
//     green (fiber) stack and can call other green functions freely

#pragma once

#include <logos/hrpc/schema.hpp>
#include <logos/reactor/features.hpp>

namespace logos::hrpc {

// Forward declaration — avoid circular include with context.hpp
class Context;

// ---------------------------------------------------------------------------
// Thunks — defined in plain logos::hrpc namespace with explicit LOGOS_GREEN.
//
// green_handler_invoke_thunk: called from GreenHandlerFn::operator() (green).
//   Calls the stored functor's operator()(ctx) — if the functor is marked
//   LOGOS_GREEN, green→green; otherwise green→red (auto-switch, OK for
//   handlers that do not call green primitives themselves).
//
// green_handler_copy_thunk / green_handler_destroy_thunk: red helpers for
//   copying and destroying the heap-allocated functor.
// ---------------------------------------------------------------------------

template<typename F>
LOGOS_GREEN Response green_handler_invoke_thunk(void* p, Context& ctx) noexcept {
    return (*static_cast<F*>(p))(ctx);
}

template<typename F>
LOGOS_RED void* green_handler_copy_thunk(void* p) noexcept {
    return new F(*static_cast<const F*>(p));
}

template<typename F>
LOGOS_RED void green_handler_destroy_thunk(void* p) noexcept {
    delete static_cast<F*>(p);
}

// ---------------------------------------------------------------------------
// GreenHandlerFn — copyable type-erased handler callable.
//
// operator() is LOGOS_GREEN so handler bodies that call green primitives
// (ctx.pop, ctx.push, session.call, etc.) compile and run correctly.
//
// Constructor / destructor / copy / move are LOGOS_RED because they touch
// the heap (new/delete) which must run on the system stack.
// ---------------------------------------------------------------------------
class GreenHandlerFn {
public:
    using InvokeFn  = Response (*)(void*, Context&) noexcept;
    using CopyFn    = void* (*)(void*) noexcept;
    using DestroyFn = void (*)(void*) noexcept;

    LOGOS_RED GreenHandlerFn() noexcept = default;

    template<typename F>
    LOGOS_RED GreenHandlerFn(F&& f) noexcept
        : invoke_ (&green_handler_invoke_thunk<std::decay_t<F>>)
        , copy_   (&green_handler_copy_thunk<std::decay_t<F>>)
        , destroy_(&green_handler_destroy_thunk<std::decay_t<F>>)
        , data_   (new std::decay_t<F>(std::forward<F>(f)))
    {}

    LOGOS_RED GreenHandlerFn(const GreenHandlerFn& o) noexcept
        : invoke_ (o.invoke_), copy_(o.copy_), destroy_(o.destroy_)
        , data_  (o.copy_ ? o.copy_(o.data_) : nullptr)
    {}

    LOGOS_RED GreenHandlerFn(GreenHandlerFn&& o) noexcept
        : invoke_(o.invoke_), copy_(o.copy_), destroy_(o.destroy_), data_(o.data_)
    {
        o.data_ = nullptr;
    }

    LOGOS_RED GreenHandlerFn& operator=(const GreenHandlerFn& o) noexcept {
        if (this != &o) {
            reset_();
            invoke_ = o.invoke_; copy_ = o.copy_; destroy_ = o.destroy_;
            data_ = o.copy_ ? o.copy_(o.data_) : nullptr;
        }
        return *this;
    }

    LOGOS_RED GreenHandlerFn& operator=(GreenHandlerFn&& o) noexcept {
        if (this != &o) {
            reset_();
            invoke_ = o.invoke_; copy_ = o.copy_; destroy_ = o.destroy_;
            data_ = o.data_;
            o.data_ = nullptr;
        }
        return *this;
    }

    LOGOS_RED ~GreenHandlerFn() noexcept { reset_(); }

    // Green: called from handler fiber body (LOGOS_FIBER_FN context).
    LOGOS_GREEN Response operator()(Context& ctx) noexcept {
        return invoke_(data_, ctx);
    }

    explicit operator bool() const noexcept { return invoke_ != nullptr; }

private:
    LOGOS_RED void reset_() noexcept {
        if (destroy_ && data_) { destroy_(data_); data_ = nullptr; }
    }

    InvokeFn  invoke_  = nullptr;
    CopyFn    copy_    = nullptr;
    DestroyFn destroy_ = nullptr;
    void*     data_    = nullptr;
};

} // namespace logos::hrpc
