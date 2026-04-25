# SPDX-License-Identifier: Apache-2.0
# stdlib/rt — native runtime support library (libstdlib_rt.a).
#
# Contains arch-specific code that the Logos stdlib externs into:
#   fiber_ctx.S      — x86-64 context switch, fiber entry trampoline,
#                      per-thread scheduler/thread TLS pointers,
#                      atomic load/store for SPSC.
#   thread_uring.c   — thin C wrappers for liburing static-inline helpers
#                      (io_uring_prep_poll_add, cqe_seen, wait_cqe, etc.).
#
# The resulting archive is placed in ${STDLIB_BIN_DIR} so that the test
# runner picks it up automatically (run_test.sh collects every *.a from
# every -L directory it receives).

set(STDLIB_BIN_DIR ${CMAKE_BINARY_DIR}/stdlib_bin)
file(MAKE_DIRECTORY ${STDLIB_BIN_DIR})

add_library(logos_rt STATIC fiber_ctx.S thread_uring.c env.c metaprog_stubs.c)
target_include_directories(logos_rt PRIVATE /usr/include)
set_target_properties(logos_rt PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${STDLIB_BIN_DIR}
    # Must sort AFTER libstdlib.a so static-link order is correct:
    # libstdlib.a calls logos_fiber_switch etc.; libstdlib_rt.a provides them.
    # Shell glob of *.a is alphabetical: 'libstdlib.' < 'libstdlib_' ✓
    OUTPUT_NAME stdlib_rt)

# Copy liburing.a into STDLIB_BIN_DIR so run_test.sh's glob picks it up.
# Named libstdlib_uring.a so it sorts after libstdlib_rt.a (provider order).
set(STDLIB_URING_DST ${STDLIB_BIN_DIR}/libstdlib_uring.a)
add_custom_command(
    OUTPUT  ${STDLIB_URING_DST}
    COMMAND ${CMAKE_COMMAND} -E copy
            /usr/lib/x86_64-linux-gnu/liburing.a
            ${STDLIB_URING_DST}
    COMMENT "Copy liburing.a → libstdlib_uring.a"
)
add_custom_target(stdlib_uring ALL DEPENDS ${STDLIB_URING_DST})
