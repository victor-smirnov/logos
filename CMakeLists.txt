cmake_minimum_required(VERSION 3.28)

# Jenny toolchain (custom Clang 21).
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/toolchains/jenny.cmake"
    CACHE STRING "Jenny (custom Clang 21) compiler toolchain")

project(logos LANGUAGES CXX C ASM)

# Default to an optimised build when the user hasn't explicitly picked one.
# Single-config generators (Ninja, Make) leave CMAKE_BUILD_TYPE empty by
# default, which gets no `-O` flags and makes the compiler ~6× slower —
# Logos' parser alone dropped from 7μs/byte to 0.4μs/byte once Release
# was enabled. Pick RelWithDebInfo so debuggers still work.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
        "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Strict warnings
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

include(GNUInstallDirs)

# ── System module path (bin-relative, baked into logosc) ─────────────────
#
# logosc resolves its system module library directory at runtime by
# readlink'ing /proc/self/exe and appending LOGOS_LIB_RELDIR. Both the
# build tree (build/bin/logosc → build/lib/logos/) and the install tree
# (<prefix>/bin/logosc → <prefix>/lib/logos/) follow the same layout
# so a single relative suffix works for both, no absolute paths baked in.
set(LOGOS_LIB_RELDIR "lib/logos" CACHE STRING
    "Path under install prefix (and mirrored in build tree) for the Logos system module library")
set(LOGOS_BUILD_LIB_DIR ${CMAKE_BINARY_DIR}/${LOGOS_LIB_RELDIR})
file(MAKE_DIRECTORY ${LOGOS_BUILD_LIB_DIR})

# Find dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLite3 REQUIRED IMPORTED_TARGET sqlite3)

# Global includes
include_directories(include)

# Enable testing early so add_test() in subdirectories is effective.
enable_testing()

# Modules
add_subdirectory(src/verification)
add_subdirectory(src/core)
add_subdirectory(src/hermes)

# The following subtrees consume the C++ Hermes API and are temporarily
# disabled while Hermes/C++ is being brought byte-for-byte in line with the
# Logos wire format.  Re-enable once the Hermes refactor is complete and
# exercisers pass.  See docs/hermes-alignment.md (TODO) or the top of
# mlir_gen_expr.cpp (ZoneBuilder) for context.
add_subdirectory(src/reactor)
add_subdirectory(src/hrpc)
add_subdirectory(src/jit)
add_subdirectory(src/compiler)
add_subdirectory(tools/peg_gen)
add_subdirectory(tools/hrpc_gen)
add_subdirectory(tools/logos-audit)
add_subdirectory(examples)

# Logos stdlib native runtime (fiber context switch, etc.)
add_subdirectory(stdlib/rt)

# Tests
add_subdirectory(tests/logos)

# ── Install rules ────────────────────────────────────────────────────────
#
# `cmake --install <build> --prefix <P>` produces a relocatable layout:
#
#   <P>/bin/logosc                       — compiler binary
#   <P>/lib/logos/liblstdlib.a           — compiled stdlib (.o + .hermes0)
#   <P>/lib/logos/liblstdlib_rt.a        — native runtime support
#   <P>/lib/logos/liblstdlib_uring.a     — liburing wrappers
#
# The compiler resolves its system module library via a suffix baked at
# build time (LOGOS_LIB_RELDIR), realpath'd off /proc/self/exe — the
# same suffix works for the build tree and the install tree, so no
# absolute path is hard-coded into the binary.
install(TARGETS logosc
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES
    ${LOGOS_BUILD_LIB_DIR}/liblstdlib.a
    ${LOGOS_BUILD_LIB_DIR}/liblstdlib_rt.a
    ${LOGOS_BUILD_LIB_DIR}/liblstdlib_uring.a
    DESTINATION ${LOGOS_LIB_RELDIR})
