# SPDX-License-Identifier: Apache-2.0
# stdlib/rt — native runtime support libraries.
#
# Two archives, separated by ORC RuntimeDyld compatibility:
#
#   liblstdlib_fibers.a  — fiber_ctx.S only.
#                          Carries `R_X86_64_GOTTPOFF` TLS relocations
#                          that ORC's RuntimeDyld can't resolve, so it
#                          must NOT be in the metacall-JIT archive set
#                          (main.cpp filters `*_fibers.a` out of
#                          `archive_paths`). It IS in the user link line
#                          via the install/build lib/logos directory.
#
#   liblstdlib_rt.a      — atomic_ops.S, thread_uring.c, env.c,
#                          metaprog_stubs.c. Zero TLS relocations →
#                          safe for both link-time use and JIT load.
#
# Glob order ensures static-link finds fiber symbols correctly:
#   liblstdlib.a < liblstdlib_fibers.a < liblstdlib_rt.a < liblstdlib_uring.a
# (period < underscore, then alpha within `_`).
#
# The resulting archives are placed in ${STDLIB_BIN_DIR} so that the
# test runner picks them up automatically (run_test.sh collects every
# *.a from every -L directory it receives).

set(STDLIB_BIN_DIR ${LOGOS_BUILD_LIB_DIR})

add_library(logos_fibers STATIC fiber_ctx.S)
set_target_properties(logos_fibers PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${STDLIB_BIN_DIR}
    OUTPUT_NAME lstdlib_fibers)

add_library(logos_rt STATIC atomic_ops.S thread_uring.c env.c metaprog_stubs.c fs_meta.c)
target_include_directories(logos_rt PRIVATE /usr/include)
set_target_properties(logos_rt PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${STDLIB_BIN_DIR}
    OUTPUT_NAME lstdlib_rt)

# Process-global block-ops registry for the persistent containers
# (std.data.persistent.bt.registry). C++17 + std::unordered_map +
# std::mutex. Kept in its own archive so the lforge dogfood test
# (which only mirrors lstdlib_rt + lstdlib_fibers) doesn't have to
# learn cpp_sources support yet. Naming sorts after lstdlib_rt so
# the test runner's *.a glob picks it up consistently.
add_library(logos_block_ops STATIC block_ops_registry.cpp)
set_target_properties(logos_block_ops PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    ARCHIVE_OUTPUT_DIRECTORY ${STDLIB_BIN_DIR}
    OUTPUT_NAME lstdlib_z_block_ops)

# Copy liburing.a into STDLIB_BIN_DIR so run_test.sh's glob picks it up.
# Named liblstdlib_uring.a so it sorts after liblstdlib_rt.a (provider order).
set(STDLIB_URING_DST ${STDLIB_BIN_DIR}/liblstdlib_uring.a)
add_custom_command(
    OUTPUT  ${STDLIB_URING_DST}
    COMMAND ${CMAKE_COMMAND} -E copy
            /usr/lib/x86_64-linux-gnu/liburing.a
            ${STDLIB_URING_DST}
    COMMENT "Copy liburing.a → liblstdlib_uring.a"
)
add_custom_target(stdlib_uring ALL DEPENDS ${STDLIB_URING_DST})
