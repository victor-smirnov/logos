# SPDX-License-Identifier: Apache-2.0
# stdlib/rt — native runtime support library (liblogos_rt.a).
#
# Contains arch-specific code that the Logos stdlib externs into:
#   fiber_ctx.S   — x86-64 context switch, fiber entry trampoline,
#                   per-thread scheduler pointer.
#
# The resulting archive is placed in ${STDLIB_BIN_DIR} so that the test
# runner picks it up automatically (run_test.sh collects every *.a from
# every -L directory it receives).

set(STDLIB_BIN_DIR ${CMAKE_BINARY_DIR}/stdlib_bin)
file(MAKE_DIRECTORY ${STDLIB_BIN_DIR})

add_library(logos_rt STATIC fiber_ctx.S)
set_target_properties(logos_rt PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${STDLIB_BIN_DIR}
    # Must sort AFTER libstdlib.a so static-link order is correct:
    # libstdlib.a calls logos_fiber_switch etc.; libstdlib_rt.a provides them.
    # Shell glob of *.a is alphabetical: 'libstdlib.' < 'libstdlib_' ✓
    OUTPUT_NAME stdlib_rt)

# Ensure the archive lands in stdlib_bin/ with the exact name liblogos_rt.a.
# add_library already prefixes "lib", so OUTPUT_NAME=logos_rt → liblogos_rt.a.
