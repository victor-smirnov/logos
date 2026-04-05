// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// LOGOS_HAS_GREEN_STACKS — compile-time switch between stack modes.
//
//   1 → green mode (Jenny / Clang 21):
//         segmented 1 KB initial stacks; grow on demand via __morestack
//   0 → classic mode (any compiler):
//         fixed-size stacks from per-Scheduler StackPool
//
// Can be overridden on the command line: -DLOGOS_HAS_GREEN_STACKS=1

#pragma once

#ifndef LOGOS_HAS_GREEN_STACKS
#  ifdef __has_feature
#    if __has_feature(green_fibers)
#      define LOGOS_HAS_GREEN_STACKS 1
#    else
#      define LOGOS_HAS_GREEN_STACKS 0
#    endif
#  else
#    define LOGOS_HAS_GREEN_STACKS 0
#  endif
#endif

// ---------------------------------------------------------------------------
// LOGOS_GREEN / LOGOS_RED — function coloring for Jenny (Clang 21).
//
// LOGOS_GREEN marks a function as "fiber-stack-safe":
//   - Has split-stack overflow protection (compiler-generated prologue).
//   - Runs on the fiber's segmented (green) stack.
//   - When a LOGOS_GREEN function calls a LOGOS_RED function, Jenny
//     automatically switches RSP to the system (red) thread stack for the
//     duration of the call, then restores the green RSP on return.
//
// LOGOS_RED marks a function that must run on the system thread stack.
//   Always runs via the red-stack mechanism when called from green context.
//
// In classic mode (LOGOS_HAS_GREEN_STACKS=0) both macros expand to nothing.
// ---------------------------------------------------------------------------
#if LOGOS_HAS_GREEN_STACKS

#  define LOGOS_GREEN    [[clang::green]]
#  define LOGOS_RED      [[clang::red]]
// Suffix for fiber-body lambdas: marks operator() as green so the fiber
// trampoline (green) can call it.  Usage: [captures]() LOGOS_FIBER_FN { ... }
#  define LOGOS_FIBER_FN noexcept [[clang::green]]
#else
#  define LOGOS_GREEN
#  define LOGOS_RED
#  define LOGOS_FIBER_FN
#endif

// ---------------------------------------------------------------------------
// LOGOS_NS_BEGIN / LOGOS_NS_END — open/close namespace logos::reactor with
// the appropriate coloring.
//
// C++ does not allow attributes on nested-namespace definitions (logos::reactor
// in one token), so in green mode we expand to two separate namespace scopes.
// In classic mode both macros produce the conventional single-token form.
// ---------------------------------------------------------------------------
#if LOGOS_HAS_GREEN_STACKS
#  define LOGOS_NS_BEGIN  namespace logos { namespace [[clang::green]] reactor {
#  define LOGOS_NS_END    } /* reactor */ } /* logos */
#else
#  define LOGOS_NS_BEGIN  namespace logos::reactor {
#  define LOGOS_NS_END    } // namespace logos::reactor
#endif
