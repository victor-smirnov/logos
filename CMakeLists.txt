cmake_minimum_required(VERSION 3.28)

# Toolchain: Logos builds with the distribution's stock C++23 toolchain
# (any recent clang or gcc) against stock LLVM/MLIR dev packages. The Jenny
# (custom Clang 21) fork is no longer required. To pick a specific stock
# compiler pass -DCMAKE_CXX_COMPILER=clang++-20 (or set CC/CXX). The legacy
# Jenny toolchain remains available opt-in via
#   -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/jenny.cmake

project(logos VERSION 0.28.0 LANGUAGES CXX C ASM)

# ── Version & parallel-install slot ──────────────────────────────────────
# Single source of truth: project(VERSION) above + the pre-release marker
# below. Three language-development stages read off the version:
#   preview     LOGOS_VERSION_PRE=preview, major 0 → 0.Y.Z-preview (nothing guaranteed)
#   stabilising LOGOS_VERSION_PRE="",     major 0 → 0.Y.Z         (tools stable, lang fluid)
#   stable      major >= 1                           → X.Y.Z         (full guarantees)
# `-preview` is ORTHOGONAL — it attaches to any version, a version-safe space for
# experiments.
set(LOGOS_VERSION_PRE "preview" CACHE STRING
    "Pre-release marker appended as -<PRE> (empty = released). Orthogonal to X.Y.Z.")

# FULL = X.Y.Z[-preview] — reported by --version, CPack package version.
# SLOT = X.Y[-preview]   — the parallel-install / ABI-compat boundary users pin to
#        (minor bump = ABI break; see memory project_versioning_abi). Drives the
#        versioned binary `logosc-<SLOT>` and stdlib dir `lib/logos/<SLOT>/`.
set(LOGOS_VERSION_FULL "${PROJECT_VERSION}")
set(LOGOS_VERSION_SLOT "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
if(LOGOS_VERSION_PRE)
    set(LOGOS_VERSION_FULL "${LOGOS_VERSION_FULL}-${LOGOS_VERSION_PRE}")
    set(LOGOS_VERSION_SLOT "${LOGOS_VERSION_SLOT}-${LOGOS_VERSION_PRE}")
endif()

# Release vs snapshot. A release is created EXPLICITLY (-DLOGOS_RELEASE=ON, set by
# the release flow once the ABI check has fixed the version): it owns the clean
# slot X.Y[-preview]. EVERY other build is a SNAPSHOT and gets a git discriminator
# (branch + short commit [+ -dirty]) appended to the slot, so it installs into its
# OWN slot and can never masquerade as / collide with a release or another build:
#   - `cpack` straight off main HEAD          → ...-main-g<sha>      (caught by default)
#   - a feature branch / an uncontrolled fork → ...-<branch>-g<sha>
#   - a dirty working tree                    → ...-g<sha>-dirty
# Default OFF means an accidental release-shaped package is impossible without opt-in.
option(LOGOS_RELEASE
    "Official release build → clean X.Y[-preview] install slot. OFF (default) = snapshot, slot git-disambiguated." OFF)
if(LOGOS_RELEASE)
    message(STATUS "Logos ${LOGOS_VERSION_FULL}  [RELEASE]  install slot: logosc-${LOGOS_VERSION_SLOT}")
else()
    set(_disc "snapshot")   # no-git fallback (e.g. tarball) — still distinct from any release slot
    set(_clean_commit OFF)  # ON only for a non-dirty git checkout (sha is then a deterministic build id)
    find_package(Git QUIET)
    if(Git_FOUND AND IS_DIRECTORY ${CMAKE_SOURCE_DIR}/.git)
        execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short=8 HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE _git_sha
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE _git_branch
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        execute_process(COMMAND ${GIT_EXECUTABLE} status --porcelain --untracked-files=no
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE _git_dirty
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(_git_sha)
            string(TOLOWER "${_git_branch}" _git_branch)
            string(REGEX REPLACE "[^a-z0-9]+" "-" _git_branch "${_git_branch}")  # slot/package-name safe
            string(REGEX REPLACE "^-+|-+$" "" _git_branch "${_git_branch}")
            if(NOT _git_branch OR _git_branch STREQUAL "head")
                set(_git_branch "detached")
            endif()
            set(_disc "${_git_branch}-g${_git_sha}")
            if(_git_dirty)
                set(_disc "${_disc}-dirty")
            else()
                set(_clean_commit ON)   # commit id alone identifies this build
            endif()
        endif()
    endif()
    # Install SLOT uses the STABLE disc (one slot per branch/snapshot — a build
    # timestamp must NOT churn the install dir).
    set(LOGOS_VERSION_SLOT "${LOGOS_VERSION_SLOT}-${_disc}")
    # FULL (semver build-metadata, what `--version` reports + the @abi stamp + the
    # lforge rebuild-on-toolchain-change check) must DISTINGUISH otherwise-identical
    # builds: a clean commit is already unique (sha), but a dirty tree or a no-git
    # snapshot (e.g. the Docker .deb, which excludes .git) is not — append a build
    # timestamp so each such toolchain build gets a distinct version.
    set(_full_disc "${_disc}")
    if(NOT _clean_commit)
        string(TIMESTAMP _build_ts "%Y%m%dT%H%M%SZ" UTC)
        set(_full_disc "${_disc}.${_build_ts}")
    endif()
    set(LOGOS_VERSION_FULL "${LOGOS_VERSION_FULL}+${_full_disc}")
    message(STATUS "Logos ${LOGOS_VERSION_FULL}  [SNAPSHOT — not a release]  install slot: logosc-${LOGOS_VERSION_SLOT}")
    message(STATUS "  snapshot installs into its own slot; pass -DLOGOS_RELEASE=ON for an official release build")
endif()

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
# Optimization level for Logos library/package archives built via
# `logosc --emit-module` (stdlib layers, peg_gen_logos, …). The emit-module
# path used to hardcode -O0 ("skip opt pipeline" for fast builds); logosc now
# honors -O on that path. -O2 here makes the shipped stdlib + tools actually
# optimized (measured ~3.34× on the peg_gen_logos parser). Set to empty for the
# old fast-but-unoptimized build. Affects build time, not correctness (gated by
# ctest + the peg_gen_logos oracle).
set(LOGOS_LIB_OPT "-O2" CACHE STRING "logosc -O level for --emit-module library archives (empty = -O0)")

set(LOGOS_LLVM_DIR  "" CACHE PATH "LLVM cmake dir (empty = auto-discover stock)")
set(LOGOS_MLIR_DIR  "" CACHE PATH "MLIR cmake dir (empty = auto-discover stock)")
set(LOGOS_CLANG_DIR "" CACHE PATH "Clang cmake dir (empty = auto-discover stock)")
if(NOT LOGOS_LLVM_DIR)
    foreach(_v 22 21 20 19 18)
        if(EXISTS "/usr/lib/llvm-${_v}/lib/cmake/llvm/LLVMConfig.cmake")
            set(LOGOS_LLVM_DIR  "/usr/lib/llvm-${_v}/lib/cmake/llvm"  CACHE PATH "" FORCE)
            set(LOGOS_MLIR_DIR  "/usr/lib/llvm-${_v}/lib/cmake/mlir"  CACHE PATH "" FORCE)
            set(LOGOS_CLANG_DIR "/usr/lib/llvm-${_v}/lib/cmake/clang" CACHE PATH "" FORCE)
            set(LOGOS_LLVM_MAJOR "${_v}" CACHE STRING "Discovered stock LLVM major" FORCE)
            message(STATUS "logos: auto-discovered stock LLVM/MLIR series ${_v} at /usr/lib/llvm-${_v}")
            break()
        endif()
    endforeach()
endif()
# Fallback: recover the major from an explicitly-set (Debian-layout) cmake dir.
if(NOT DEFINED LOGOS_LLVM_MAJOR AND LOGOS_LLVM_DIR MATCHES "llvm-([0-9]+)")
    set(LOGOS_LLVM_MAJOR "${CMAKE_MATCH_1}")
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

# Coercion arc S2: the type-mismatch verdict is emitted by exactly one
# function (SemaChecker::expect_type). Mechanically enforced — see the script.
add_test(NAME lint_mismatch_monopoly
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/lint-mismatch-monopoly.sh)
set_tests_properties(lint_mismatch_monopoly PROPERTIES LABELS "lint")

# Modules
add_subdirectory(src/verification)
add_subdirectory(src/core)
add_subdirectory(src/writ)

# The following subtrees consume the C++ Writ API and are temporarily
# disabled while Writ/C++ is being brought byte-for-byte in line with the
# Logos wire format.  Re-enable once the Writ refactor is complete and
# exercisers pass.  See docs/writ-alignment.md (TODO) or the top of
# mlir_gen_expr.cpp (ZoneBuilder) for context.
add_subdirectory(src/reactor)
add_subdirectory(src/hrpc)
add_subdirectory(src/jit)
add_subdirectory(src/compiler)
add_subdirectory(tools/peg_gen_cpp)
add_subdirectory(tools/hrpc_gen)
add_subdirectory(tools/logos-audit)
add_subdirectory(tools/lforge)
add_subdirectory(tools/docgen)
add_subdirectory(tools/peg_gen_logos)
add_subdirectory(examples)

# Logos stdlib native runtime (fiber context switch, etc.)
add_subdirectory(stdlib/rt)

# Tests
add_subdirectory(tests/logos)

# ── Install rules ────────────────────────────────────────────────────────
#
# Self-contained versioned tree (LLVM-style): every file of a version lives under
# ONE directory; the system dirs carry only symlinks. `cmake --install <build>
# --prefix <P>`:
#
#   <P>/lib/logos/<SLOT>/bin/logosc      — the real compiler binary
#   <P>/lib/logos/<SLOT>/lib/*.a         — its stdlib + runtime archives
#   <P>/bin/logosc-<SLOT> -> ../lib/logos/<SLOT>/bin/logosc   — versioned PATH entry
#   <P>/bin/logosc        -> logosc-<SLOT>                    — unversioned selector
#                                          (symlink for make install;
#                                           update-alternatives in the .deb/.rpm)
#
# The binary finds its stdlib relative to itself (<exe_dir>/../lib), so the tree
# is relocatable and `rm -rf <P>/lib/logos/<SLOT>` removes a version cleanly.
# Future per-version docs/resources for AI agents live under the SAME tree (e.g.
# <P>/lib/logos/<SLOT>/share/…) and are located via `logosc --print-prefix`.
set(LOGOS_TREE_RELDIR "${LOGOS_LIB_RELDIR}/${LOGOS_VERSION_SLOT}")  # lib/logos/<SLOT>

# Unversioned bin/logosc as a plain symlink: ON for `make install` (single dev
# install), OFF for packages — there alternatives owns /usr/bin/logosc so slots
# coexist without a file clash.
option(LOGOS_INSTALL_UNVERSIONED_SYMLINK
    "Install bin/logosc -> logosc-<SLOT> as a file (OFF for coexisting packages; alternatives instead)" ON)

# Real binary + stdlib into the versioned tree.
install(TARGETS logosc RUNTIME DESTINATION ${LOGOS_TREE_RELDIR}/bin)
install(FILES
    ${LOGOS_BUILD_LIB_DIR}/liblogos-lang.a
    ${LOGOS_BUILD_LIB_DIR}/liblogos-mem.a
    ${LOGOS_BUILD_LIB_DIR}/liblogos-lcm.a
    ${LOGOS_BUILD_LIB_DIR}/liblogos-std.a
    ${LOGOS_BUILD_LIB_DIR}/liblstdlib_fibers.a
    ${LOGOS_BUILD_LIB_DIR}/liblstdlib_rt.a
    DESTINATION ${LOGOS_TREE_RELDIR}/lib)
if(LOGOS_BUNDLE_URING_STATIC)
    install(FILES ${LOGOS_BUILD_LIB_DIR}/liblstdlib_uring.a
        DESTINATION ${LOGOS_TREE_RELDIR}/lib)
endif()

# ── gdb/lldb pretty-printers (debugging Logos binaries built with `logosc -g`) ──
# Installed PER SLOT: the enum-metadata schema and Writ layout the printers
# decode are version-coupled to the compiler, so each version ships matching
# printers under its own tree. Locate at runtime via:
#   source "$(logosc --print-prefix)/share/gdb/logos_gdb.py"
# (logos_gdb.py auto-loads logos_writ_gdb.py from the same dir.) Tracked by
# both CPack DEB and RPM (plain install(FILES), unlike install(CODE) symlinks).
set(LOGOS_GDB_SCRIPTS
    tools/gdb/logos_gdb.py
    tools/gdb/logos_writ_gdb.py)
# Build-tree mirror so `$(logosc --print-prefix)/share/gdb` resolves pre-install
# too (build prefix = CMAKE_BINARY_DIR). COPYONLY re-copies when a script changes.
foreach(_gdb_script ${LOGOS_GDB_SCRIPTS})
    get_filename_component(_gdb_name ${_gdb_script} NAME)
    configure_file(${CMAKE_SOURCE_DIR}/${_gdb_script}
                   ${CMAKE_BINARY_DIR}/share/gdb/${_gdb_name} COPYONLY)
endforeach()
install(FILES ${LOGOS_GDB_SCRIPTS} DESTINATION ${LOGOS_TREE_RELDIR}/share/gdb)

# LLDB data formatters (same per-slot rationale; lldb has its own Python API, so
# this is a separate module from the gdb printers). Located via
# `$(logosc --print-prefix)/share/lldb/logos_lldb.py`.
configure_file(${CMAKE_SOURCE_DIR}/tools/gdb/logos_lldb.py
               ${CMAKE_BINARY_DIR}/share/lldb/logos_lldb.py COPYONLY)
install(FILES tools/gdb/logos_lldb.py DESTINATION ${LOGOS_TREE_RELDIR}/share/lldb)

# logos-gdb / logos-lldb launchers (rust-gdb / rust-lldb style): self-locate the
# formatters in their own slot tree (<slot>/bin/logos-gdb → <slot>/share/gdb;
# logos-lldb → <slot>/share/lldb). Installed beside logosc in the slot bin/,
# mirrored to the build tree, and exposed on PATH like logosc below.
configure_file(${CMAKE_SOURCE_DIR}/tools/gdb/logos-gdb
               ${CMAKE_BINARY_DIR}/bin/logos-gdb COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/tools/gdb/logos-lldb
               ${CMAKE_BINARY_DIR}/bin/logos-lldb COPYONLY)
install(PROGRAMS tools/gdb/logos-gdb tools/gdb/logos-lldb
        DESTINATION ${LOGOS_TREE_RELDIR}/bin)

# lforge — the primary Logos build tool (cargo-style; drives logosc). Installed
# beside logosc in the slot tree; exposed on PATH as an alternatives slave so it
# always tracks the selected logosc, and finds its sibling logosc via /proc/self/exe.
install(PROGRAMS ${CMAKE_BINARY_DIR}/bin/lforge DESTINATION ${LOGOS_TREE_RELDIR}/bin)

# Versioned PATH symlink bin/logosc-<SLOT> -> ../lib/logos/<SLOT>/bin/logosc.
# Staged as a real symlink + install(DIRECTORY) so it is TRACKED by BOTH CPack
# generators (install(CODE) symlinks are invisible to CPack RPM's %files).
# Clear the WHOLE staging dir (not just this slot's link): reconfiguring an
# existing build dir to a different slot would otherwise leave stale versioned
# symlinks here for install(DIRECTORY) to ship as dangling links.
file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/_pkglinks")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_pkglinks")
file(CREATE_LINK "../${LOGOS_TREE_RELDIR}/bin/logosc"
     "${CMAKE_BINARY_DIR}/_pkglinks/logosc-${LOGOS_VERSION_SLOT}" SYMBOLIC)
# logos-gdb-<SLOT> / logos-lldb-<SLOT> -> ../lib/logos/<SLOT>/bin/… (versioned PATH).
file(CREATE_LINK "../${LOGOS_TREE_RELDIR}/bin/logos-gdb"
     "${CMAKE_BINARY_DIR}/_pkglinks/logos-gdb-${LOGOS_VERSION_SLOT}" SYMBOLIC)
file(CREATE_LINK "../${LOGOS_TREE_RELDIR}/bin/logos-lldb"
     "${CMAKE_BINARY_DIR}/_pkglinks/logos-lldb-${LOGOS_VERSION_SLOT}" SYMBOLIC)
# lforge-<SLOT> -> ../lib/logos/<SLOT>/bin/lforge (versioned PATH entry).
file(CREATE_LINK "../${LOGOS_TREE_RELDIR}/bin/lforge"
     "${CMAKE_BINARY_DIR}/_pkglinks/lforge-${LOGOS_VERSION_SLOT}" SYMBOLIC)
install(DIRECTORY "${CMAKE_BINARY_DIR}/_pkglinks/" DESTINATION ${CMAKE_INSTALL_BINDIR})

# Unversioned bin/logosc -> logosc-<SLOT> (make install only; packages use
# alternatives via the post/postun scriptlets below).
if(LOGOS_INSTALL_UNVERSIONED_SYMLINK)
    install(CODE "
        set(_bindir \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
        execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink
            \"logosc-${LOGOS_VERSION_SLOT}\" \"\${_bindir}/logosc\")
        execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink
            \"logos-gdb-${LOGOS_VERSION_SLOT}\" \"\${_bindir}/logos-gdb\")
        execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink
            \"logos-lldb-${LOGOS_VERSION_SLOT}\" \"\${_bindir}/logos-lldb\")
    ")
endif()

# ── ABI spec (versioning + ABI tracking) ─────────────────────────────────
# Regenerate the committed abi/logos.abi from the freshly-built stdlib. Its git
# history IS the ABI changelog; `abi-analyze` diffs two revisions to qualify
# changes as ABI-preserving vs ABI-breaking (the minor-bump gate). Not built by
# ALL — run `cmake --build <dir> --target logos-abi` after changing the surface.
add_custom_target(logos-abi
    COMMAND $<TARGET_FILE:logosc> --emit-abi -L ${LOGOS_BUILD_LIB_DIR}
            -o ${CMAKE_SOURCE_DIR}/abi/logos.abi
    DEPENDS logosc
            ${LOGOS_BUILD_LIB_DIR}/liblogos-lang.a
            ${LOGOS_BUILD_LIB_DIR}/liblogos-mem.a
            ${LOGOS_BUILD_LIB_DIR}/liblogos-lcm.a
            ${LOGOS_BUILD_LIB_DIR}/liblogos-std.a
    COMMENT "Regenerating abi/logos.abi (ABI spec)"
    VERBATIM)

# ── Packaging (.deb / .rpm via CPack) ────────────────────────────────────
#
# `cpack -G DEB` (Debian/Ubuntu) or `cpack -G RPM` (Fedora/RHEL), run from a
# build dir configured against the distro's stock LLVM. Installs logosc + the
# modular stdlib archives under /usr. Needs `dpkg-dev` resp. `rpm-build`.
# Per-slot package name so versions coexist on one system: apt installs
# `logosc-0.1-preview` and `logosc-0.2` side by side (each owns bin/logosc-<SLOT>
# + lib/logos/<SLOT>/; the unversioned bin/logosc is an alternatives link).
set(CPACK_PACKAGE_NAME    "logosc-${LOGOS_VERSION_SLOT}")
set(CPACK_PACKAGE_VERSION "${LOGOS_VERSION_FULL}")
set(CPACK_PACKAGE_VENDOR  "Victor Smirnov")
set(CPACK_PACKAGE_CONTACT "Victor Smirnov <aist11@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Logos compiler (logosc) and standard library")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_GENERATOR "DEB")

# ── DEB ──
# Depends are set explicitly rather than via dpkg-shlibdeps: Debian ships
# libMLIR.so only under the private /usr/lib/llvm-NN/lib (not a standard loader
# path), which shlibdeps cannot resolve. The libllvm/libmlir package names
# encode the LLVM major the build linked against — derived from the discovered
# toolchain (Ubuntu 24.04=20, 26.04=21), default 20 if unknown.
if(NOT LOGOS_LLVM_MAJOR)
    set(LOGOS_LLVM_MAJOR 20)
endif()
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Victor Smirnov <aist11@gmail.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION    "devel")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE
    "https://github.com/victor-smirnov/logos")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libllvm${LOGOS_LLVM_MAJOR}, libmlir-${LOGOS_LLVM_MAJOR}, libstdc++6, libc6, zlib1g, libzstd1, liburing2")
# A built Logos program is linked with `cc`/`ld` against the static stdlib
# archives, so the toolchain wants a C compiler present.
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "gcc | clang")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)  # logosc-0.1-preview_0.1.0-preview_amd64.deb

# update-alternatives: the unversioned /usr/bin/logosc selector. Priority orders
# slots so a newer one wins by default, and a released slot beats its -preview twin.
# Build the .deb with -DLOGOS_INSTALL_UNVERSIONED_SYMLINK=OFF so the symlink is
# NOT a packaged file (alternatives owns it) — that's what lets slots coexist.
math(EXPR LOGOS_ALT_PRIORITY
     "${PROJECT_VERSION_MAJOR} * 1000 + ${PROJECT_VERSION_MINOR} * 10 + 1")
if(LOGOS_VERSION_PRE)
    math(EXPR LOGOS_ALT_PRIORITY "${LOGOS_ALT_PRIORITY} - 1")  # -preview below released
endif()
configure_file(${CMAKE_SOURCE_DIR}/packaging/deb/postinst.in
               ${CMAKE_BINARY_DIR}/packaging/deb/postinst @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/packaging/deb/prerm.in
               ${CMAKE_BINARY_DIR}/packaging/deb/prerm @ONLY)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_BINARY_DIR}/packaging/deb/postinst;${CMAKE_BINARY_DIR}/packaging/deb/prerm")

# ── RPM ──
# Fedora keeps its libs on standard /usr/lib64, so rpm's automatic
# dependency generator (autoreqprov) resolves the shared-lib Requires
# correctly — no explicit list needed.
#
# RPM uses '-' as the NEVRA delimiter, so the .deb's slot name (logosc-0.1-preview)
# is invalid as an RPM Name/Version. Follow the Fedora compat-package convention
# (llvm17, gcc14): a hyphen-free concatenated name, and '~' for the pre-release
# tag in the version (RPM sorts X.Y.Z~preview before X.Y.Z). Per-slot name so RPMs
# coexist. The installed file names (bin/logosc-<SLOT>, lib/logos/<SLOT>/) are
# unchanged — only the package's own Name/Version differ.
string(REGEX REPLACE "[^a-zA-Z0-9]" "" LOGOS_VERSION_SLOT_RPM "${LOGOS_VERSION_SLOT}")
string(REPLACE "-" "~" LOGOS_VERSION_RPM "${LOGOS_VERSION_FULL}")
set(CPACK_RPM_PACKAGE_NAME    "logosc${LOGOS_VERSION_SLOT_RPM}")  # logosc01preview
set(CPACK_RPM_PACKAGE_VERSION "${LOGOS_VERSION_RPM}")             # 0.1.0~preview
set(CPACK_RPM_PACKAGE_LICENSE  "Apache-2.0 OR MIT")
set(CPACK_RPM_PACKAGE_GROUP    "Development/Languages")
set(CPACK_RPM_PACKAGE_URL      "https://github.com/victor-smirnov/logos")
set(CPACK_RPM_PACKAGE_SUMMARY  "Logos compiler (logosc) and standard library")
set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
set(CPACK_RPM_PACKAGE_SUGGESTS "gcc, liburing-devel")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)   # logosc01preview-0.1.0~preview-1.x86_64.rpm
# update-alternatives equivalent for the unversioned /usr/bin/logosc selector.
# CPack RPM's auto-%files does not capture an install(CODE)-created symlink (unlike
# CPack DEB, which tars the whole staging tree), so — as with the .deb — the .rpm
# ships only logosc-<SLOT> (build with -DLOGOS_INSTALL_UNVERSIONED_SYMLINK=OFF) and
# Fedora's `alternatives` owns /usr/bin/logosc in post/postun scriptlets.
set(CPACK_RPM_PACKAGE_REQUIRES "alternatives")
configure_file(${CMAKE_SOURCE_DIR}/packaging/rpm/post.in
               ${CMAKE_BINARY_DIR}/packaging/rpm/post @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/packaging/rpm/postun.in
               ${CMAKE_BINARY_DIR}/packaging/rpm/postun @ONLY)
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE   "${CMAKE_BINARY_DIR}/packaging/rpm/post")
set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "${CMAKE_BINARY_DIR}/packaging/rpm/postun")
include(CPack)
