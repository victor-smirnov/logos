// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// GreenFn — a move-only, type-erased callable for fiber body functions.
//
// std::move_only_function is "red" (STL is not compiled with split-stack
// support), so its internal dispatch thunk is non-green.  Storing a
// [[clang::green]] lambda in it is a compile error in Jenny's coloring model.
//
// GreenFn solves this: its namespace is [[clang::green]], so operator() and
// the invoke thunk are implicitly green.  Constructor / destructor / move are
// explicitly [[clang::red]] (they touch the heap via new/delete, which is STL
// = red; Jenny auto-switches to the system stack for those calls).
//
// Usage:
//   // In green mode, fiber body lambdas carry noexcept LOGOS_GREEN:
//   GreenFn fn = [] () noexcept LOGOS_GREEN { yield(); };
//   fn();  // green invocation
//
// Only compiled when LOGOS_HAS_GREEN_STACKS=1.  In classic mode Fiber uses
// std::move_only_function<void()> directly (no coloring constraints).

#pragma once

#include <logos/reactor/features.hpp>

#if LOGOS_HAS_GREEN_STACKS

// ---------------------------------------------------------------------------
// Thunk helpers — namespace-scoped (not lambdas) so [[clang::green]] can be
// applied explicitly.  F::operator() must itself be [[clang::green]].
// ---------------------------------------------------------------------------
namespace logos {
namespace reactor {

template<typename F>
[[clang::green]] void green_invoke_thunk(void* p) noexcept {
    (*static_cast<F*>(p))();
}

template<typename F>
void green_destroy_thunk(void* p) noexcept {
    delete static_cast<F*>(p);
}

} // namespace reactor
} // namespace logos

// ---------------------------------------------------------------------------
// GreenFn — in a [[clang::green]] namespace so that operator() (the hot
// green-stack call path) inherits the attribute implicitly.
// ---------------------------------------------------------------------------
namespace logos {
namespace [[clang::green]] reactor {

class GreenFn {
public:
    using InvokeFn = void (*)(void*) noexcept;

    [[clang::red]] GreenFn() noexcept : invoke_(nullptr), destroy_(nullptr), data_(nullptr) {}

    // Red: runs in scheduler context; allocates on heap (STL = red).
    template<typename F>
    [[clang::red]] GreenFn(F f) noexcept
        : invoke_ (logos::reactor::green_invoke_thunk<F>)
        , destroy_(logos::reactor::green_destroy_thunk<F>)
        , data_   (new F(static_cast<F&&>(f)))
    {}

    [[clang::red]] GreenFn(GreenFn&& o) noexcept
        : invoke_ (o.invoke_)
        , destroy_(o.destroy_)
        , data_   (o.data_)
    { o.invoke_ = nullptr; o.destroy_ = nullptr; o.data_ = nullptr; }

    [[clang::red]] GreenFn& operator=(GreenFn&& o) noexcept {
        if (this != &o) {
            reset_();
            invoke_  = o.invoke_;
            destroy_ = o.destroy_;
            data_    = o.data_;
            o.invoke_ = nullptr; o.destroy_ = nullptr; o.data_ = nullptr;
        }
        return *this;
    }

    [[clang::red]] ~GreenFn() noexcept { reset_(); }

    GreenFn(const GreenFn&)            = delete;
    GreenFn& operator=(const GreenFn&) = delete;

    // Green: invokes the stored callable on the fiber's green stack.
    void operator()() noexcept { invoke_(data_); }

    explicit operator bool() const noexcept { return invoke_ != nullptr; }

private:
    [[clang::red]] void reset_() noexcept {
        if (destroy_) { destroy_(data_); destroy_ = nullptr; }
        invoke_ = nullptr;
        data_   = nullptr;
    }

    InvokeFn  invoke_  = nullptr;
    InvokeFn  destroy_ = nullptr;
    void*     data_    = nullptr;
};

} // namespace reactor
} // namespace logos

#endif // LOGOS_HAS_GREEN_STACKS
