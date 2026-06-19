# Copyright 2026 Victor Smirnov
# Logos project — https://github.com/victor-smirnov/logos
#
# logos-audit — static analysis tool that enforces the Logos C++ subset.
#
# Requires the Clang/LLVM development libraries from the Jenny toolchain.
# Skipped silently if not found so the main build is unaffected.

# LOGOS_LLVM_DIR / LOGOS_CLANG_DIR are discovered (or -D-overridden) at the
# top-level CMakeLists.
find_package(LLVM CONFIG QUIET HINTS ${LOGOS_LLVM_DIR})

# logos-audit links the full Clang dev libraries (libclang*.a — shipped in
# libclang-NN-dev). A stock toolchain install that only pulled llvm/mlir does
# NOT have them, and ClangConfig.cmake hard-errors on include when its target
# files reference absent archives. Probe for the archive first and skip this
# dev-only tool entirely if it's missing, before find_package(Clang) can fail.
if (NOT LLVM_FOUND OR NOT EXISTS "${LLVM_LIBRARY_DIR}/libclangBasic.a")
    message(STATUS "logos-audit: full Clang dev libs not found — skipping")
    return()
endif()

find_package(Clang CONFIG QUIET HINTS ${LOGOS_CLANG_DIR})
if (NOT Clang_FOUND)
    message(STATUS "logos-audit: Clang cmake package not found — skipping")
    return()
endif()

message(STATUS "logos-audit: building with LLVM ${LLVM_PACKAGE_VERSION}")

# Ask the found clang for its resource directory (contains stddef.h etc.).
execute_process(
    COMMAND ${LLVM_TOOLS_BINARY_DIR}/clang --print-resource-dir
    OUTPUT_VARIABLE LOGOS_CLANG_RESOURCE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _rc
)
if (_rc OR NOT LOGOS_CLANG_RESOURCE_DIR)
    message(FATAL_ERROR "logos-audit: failed to determine clang resource dir")
endif()
message(STATUS "logos-audit: clang resource dir = ${LOGOS_CLANG_RESOURCE_DIR}")

add_executable(logos_audit src/main.cpp)

target_compile_definitions(logos_audit PRIVATE
    LOGOS_CLANG_RESOURCE_DIR="${LOGOS_CLANG_RESOURCE_DIR}"
)

# Clang headers live under ${LLVM_INCLUDE_DIRS} and ${CLANG_INCLUDE_DIRS}.
target_include_directories(logos_audit SYSTEM PRIVATE
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)

# clangTooling transitively pulls in everything we need.
target_link_libraries(logos_audit PRIVATE
    clangTooling
    clangASTMatchers
    clangFrontend
    clangSerialization
    clangAST
    clangBasic
    LLVMSupport
)

# Clang libs are built without RTTI; we must match.
target_compile_options(logos_audit PRIVATE -fno-rtti)
