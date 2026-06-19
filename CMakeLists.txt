cmake_minimum_required(VERSION 3.28)

# Toolchain: Logos builds with the distribution's stock C++23 toolchain
# (any recent clang or gcc) against stock LLVM/MLIR dev packages. The Jenny
# (custom Clang 21) fork is no longer required. To pick a specific stock
# compiler pass -DCMAKE_CXX_COMPILER=clang++-20 (or set CC/CXX). The legacy
# Jenny toolchain remains available opt-in via
#   -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/jenny.cmake

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

# Logos uses no C++20 modules. CMake 3.28 turns on per-TU module dependency
# scanning by default for C++20+ targets, which pulls in a `clang-scan-deps`
# requirement for no benefit here. Turn it off — fewer toolchain pieces, and
# faster scanning.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Strict warnings
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

include(GNUInstallDirs)

# ── LLVM / MLIR toolchain discovery ──────────────────────────────────────
#
# Logos builds against the distribution's STOCK LLVM/MLIR dev packages —
# on Ubuntu 24.04 that is `llvm-20-dev` + `libmlir-20-dev` + `mlir-20-tools`
# (or the 19 series), installed straight from the distro repos, no
# apt.llvm.org and no Jenny fork. We auto-discover the newest stock series
# present under /usr/lib/llvm-NN. Override for a custom toolchain with:
#   -DLOGOS_LLVM_DIR=/path/to/lib/cmake/llvm -DLOGOS_MLIR_DIR=/path/to/lib/cmake/mlir
set(LOGOS_LLVM_DIR  "" CACHE PATH "LLVM cmake dir (empty = auto-discover stock)")
set(LOGOS_MLIR_DIR  "" CACHE PATH "MLIR cmake dir (empty = auto-discover stock)")
set(LOGOS_CLANG_DIR "" CACHE PATH "Clang cmake dir (empty = auto-discover stock)")
if(NOT LOGOS_LLVM_DIR)
    foreach(_v 21 20 19 18)
        if(EXISTS "/usr/lib/llvm-${_v}/lib/cmake/llvm/LLVMConfig.cmake")
            set(LOGOS_LLVM_DIR  "/usr/lib/llvm-${_v}/lib/cmake/llvm"  CACHE PATH "" FORCE)
            set(LOGOS_MLIR_DIR  "/usr/lib/llvm-${_v}/lib/cmake/mlir"  CACHE PATH "" FORCE)
            set(LOGOS_CLANG_DIR "/usr/lib/llvm-${_v}/lib/cmake/clang" CACHE PATH "" FORCE)
            message(STATUS "logos: auto-discovered stock LLVM/MLIR series ${_v} at /usr/lib/llvm-${_v}")
            break()
        endif()
    endforeach()
endif()

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

# Find dependencies. PkgConfig is used by sub-targets (reactor/hrpc) which
# re-`find_package` it themselves; the SQLite3 dependency was dropped together
# with the verification SQLite trace sink.
find_package(PkgConfig REQUIRED)

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
add_subdirectory(tools/lforge)
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
#   <P>/lib/logos/liblogos-{lang,mem,std}.a — modular stdlib (.o + .hermes0)
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
    ${LOGOS_BUILD_LIB_DIR}/liblogos-lang.a
    ${LOGOS_BUILD_LIB_DIR}/liblogos-mem.a
    ${LOGOS_BUILD_LIB_DIR}/liblogos-std.a
    ${LOGOS_BUILD_LIB_DIR}/liblstdlib_fibers.a
    ${LOGOS_BUILD_LIB_DIR}/liblstdlib_rt.a
    ${LOGOS_BUILD_LIB_DIR}/liblstdlib_uring.a
    DESTINATION ${LOGOS_LIB_RELDIR})

# ── Packaging (.deb via CPack) ───────────────────────────────────────────
#
# `cpack -G DEB` (run from a build dir configured against stock distro LLVM)
# produces a relocatable Debian package installing logosc + the modular stdlib
# archives under /usr. Needs `dpkg-dev`.
#
# Depends are set explicitly rather than via dpkg-shlibdeps: the distro ships
# libMLIR.so only under the private /usr/lib/llvm-NN/lib (not a standard
# loader path), which shlibdeps cannot resolve. The list covers what logosc
# itself needs to RUN plus the linker/runtime a built Logos program needs.
# libllvm/libmlir package names encode the LLVM major the build linked
# against (stock Ubuntu 24.04 = 20); bump together with the toolchain.
set(CPACK_PACKAGE_NAME    "logos")
set(CPACK_PACKAGE_VERSION "0.1.0")
set(CPACK_PACKAGE_VENDOR  "Victor Smirnov")
set(CPACK_PACKAGE_CONTACT "Victor Smirnov <aist11@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Logos compiler (logosc) and standard library")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Victor Smirnov <aist11@gmail.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION    "devel")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE
    "https://github.com/victor-smirnov/logos")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libllvm20, libmlir-20, libstdc++6, libc6, zlib1g, libzstd1, liburing2")
# A built Logos program is linked with `cc`/`ld` against the static stdlib
# archives, so the toolchain wants a C compiler present.
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "gcc | clang")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)  # logos_0.1.0_amd64.deb
include(CPack)
