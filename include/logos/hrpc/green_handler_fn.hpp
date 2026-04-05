// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// HandlerFn helper — a copyable, type-erased callable for HRPC handler functions.
//
// Replaces std::function<Response(Context&)> for handler storage so that
// handler bodies can call fiber-blocking reactor primitives such as
// ctx.pop(), ctx.push(), and session.call().

#pragma once

#include <logos/hrpc/schema.hpp>

namespace logos::hrpc {

// Forward declaration — avoid circular include with context.hpp
class Context;

// ---------------------------------------------------------------------------
// Thunks — type-erased invoke / copy / destroy helpers.
// ---------------------------------------------------------------------------

template<typename F>
Response green_handler_invoke_thunk(void* p, Context& ctx) noexcept {
    return (*static_cast<F*>(p))(ctx);
}

template<typename F>
void* green_handler_copy_thunk(void* p) noexcept {
    return new F(*static_cast<const F*>(p));
}

template<typename F>
void green_handler_destroy_thunk(void* p) noexcept {
    delete static_cast<F*>(p);
}

// ---------------------------------------------------------------------------
// GreenHandlerFn — copyable type-erased handler callable.
// ---------------------------------------------------------------------------
class GreenHandlerFn {
public:
    using InvokeFn  = Response (*)(void*, Context&) noexcept;
    using CopyFn    = void* (*)(void*) noexcept;
    using DestroyFn = void (*)(void*) noexcept;

    GreenHandlerFn() noexcept = default;

    template<typename F>
    GreenHandlerFn(F&& f) noexcept
        : invoke_ (&green_handler_invoke_thunk<std::decay_t<F>>)
        , copy_   (&green_handler_copy_thunk<std::decay_t<F>>)
        , destroy_(&green_handler_destroy_thunk<std::decay_t<F>>)
        , data_   (new std::decay_t<F>(std::forward<F>(f)))
    {}

    GreenHandlerFn(const GreenHandlerFn& o) noexcept
        : invoke_ (o.invoke_), copy_(o.copy_), destroy_(o.destroy_)
        , data_  (o.copy_ ? o.copy_(o.data_) : nullptr)
    {}

    GreenHandlerFn(GreenHandlerFn&& o) noexcept
        : invoke_(o.invoke_), copy_(o.copy_), destroy_(o.destroy_), data_(o.data_)
    {
        o.data_ = nullptr;
    }

    GreenHandlerFn& operator=(const GreenHandlerFn& o) noexcept {
        if (this != &o) {
            reset_();
            invoke_ = o.invoke_; copy_ = o.copy_; destroy_ = o.destroy_;
            data_ = o.copy_ ? o.copy_(o.data_) : nullptr;
        }
        return *this;
    }

    GreenHandlerFn& operator=(GreenHandlerFn&& o) noexcept {
        if (this != &o) {
            reset_();
            invoke_ = o.invoke_; copy_ = o.copy_; destroy_ = o.destroy_;
            data_ = o.data_;
            o.data_ = nullptr;
        }
        return *this;
    }

    ~GreenHandlerFn() noexcept { reset_(); }

    Response operator()(Context& ctx) noexcept {
        return invoke_(data_, ctx);
    }

    explicit operator bool() const noexcept { return invoke_ != nullptr; }

private:
    void reset_() noexcept {
        if (destroy_ && data_) { destroy_(data_); data_ = nullptr; }
    }

    InvokeFn  invoke_  = nullptr;
    CopyFn    copy_    = nullptr;
    DestroyFn destroy_ = nullptr;
    void*     data_    = nullptr;
};

} // namespace logos::hrpc
