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
#                          metaprog_stubs.c, fiber_stack.c, clock.c. Zero TLS relocations →
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

add_library(logos_rt STATIC atomic_ops.S thread_uring.c env.c metaprog_stubs.c fs_meta.c fmt_native.c test_recovery.c fiber_stack.c clock.c)
target_include_directories(logos_rt PRIVATE /usr/include)
# Position-independent: these objects are loaded into the metaprog ORC JIT,
# which may map them anywhere in the address space. Non-PIC codegen emits
# absolute R_X86_64_32 (Pointer32) relocations that overflow when the JIT
# places code/data above 4 GiB — observed on Fedora (whose clang doesn't
# default to PIE) but not on Ubuntu (PIE default). -fPIC makes the relocations
# GOT/PC-relative so the JIT can place them anywhere, on every distro.
set_target_properties(logos_rt PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${STDLIB_BIN_DIR}
    OUTPUT_NAME lstdlib_rt
    POSITION_INDEPENDENT_CODE ON)

# Bundle the system STATIC liburing.a as liblstdlib_uring.a so run_test.sh's
# lib*.a glob picks it up (named to sort after liblstdlib_rt.a — provider
# order). Located portably across distros (Debian multiarch /usr/lib/<triple>,
# Fedora /usr/lib64, …). Some distros (Fedora) ship only the shared
# liburing.so with NO static archive — there we skip the bundle and consumers
# link `-luring` (shared) instead. LOGOS_BUNDLE_URING_STATIC tells the
# top-level install rule whether the file exists.
set(STDLIB_URING_DST ${STDLIB_BIN_DIR}/liblstdlib_uring.a)
find_file(LOGOS_LIBURING_STATIC
    NAMES liburing.a
    PATHS /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib /lib/x86_64-linux-gnu)
if(LOGOS_LIBURING_STATIC)
    add_custom_command(
        OUTPUT  ${STDLIB_URING_DST}
        COMMAND ${CMAKE_COMMAND} -E copy ${LOGOS_LIBURING_STATIC} ${STDLIB_URING_DST}
        COMMENT "Copy ${LOGOS_LIBURING_STATIC} → liblstdlib_uring.a"
        DEPENDS ${LOGOS_LIBURING_STATIC}
    )
    # `stdlib_uring` target carries the copy; dependents (lforge, tests) keep
    # depending on it unconditionally.
    add_custom_target(stdlib_uring ALL DEPENDS ${STDLIB_URING_DST})
    set(LOGOS_BUNDLE_URING_STATIC ON CACHE INTERNAL "static liburing.a bundled")
else()
    # No static liburing.a (e.g. Fedora): keep the target so dependents still
    # resolve, but it produces nothing — consumers link the shared -luring.
    message(STATUS "stdlib_uring: no static liburing.a — consumers link -luring (shared)")
    add_custom_target(stdlib_uring ALL)
    set(LOGOS_BUNDLE_URING_STATIC OFF CACHE INTERNAL "static liburing.a bundled")
endif()
