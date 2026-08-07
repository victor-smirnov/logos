# Copyright 2026 Victor Smirnov
# Logos project — https://github.com/victor-smirnov/logos
#
# logos_jit — reusable LLVM ORC LLJIT wrapper.
#
# Phase 4 / Slice 1: minimal compile-and-run for hand-rolled LLVM IR.
# No Writ integration, no Logos-pipeline integration, no split-stack —
# just enough surface to bring up ORC and prove the link works.

# LOGOS_LLVM_DIR is discovered (or -D-overridden) at the top-level CMakeLists.
find_package(LLVM CONFIG QUIET HINTS ${LOGOS_LLVM_DIR})

if (NOT LLVM_FOUND)
    message(STATUS "logos_jit: LLVM dev libs not found — skipping")
    return()
endif()

add_library(logos_jit STATIC
    jit.cpp
)

target_include_directories(logos_jit PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
# SYSTEM: silence third-party LLVM header deprecations (AlignOf.h via Expected<T>).
target_include_directories(logos_jit SYSTEM PUBLIC
    ${LLVM_INCLUDE_DIRS}
)

# Distro-stock LLVM links the single libLLVM.so dylib; a static toolchain
# links the explicit component archives. Mixing the two pulls a second copy of
# LLVM's global state (see src/compiler/CMakeLists.txt for the full rationale).
if (LLVM_LINK_LLVM_DYLIB)
    set(LOGOS_JIT_LLVM_LIBS LLVM)
else()
    set(LOGOS_JIT_LLVM_LIBS
        LLVMOrcJIT LLVMExecutionEngine LLVMRuntimeDyld LLVMJITLink
        LLVMOrcShared LLVMOrcTargetProcess
        LLVMCore LLVMSupport LLVMTarget LLVMTargetParser LLVMMC
        LLVMAsmPrinter LLVMCodeGen LLVMAnalysis LLVMTransformUtils
        LLVMScalarOpts LLVMipo LLVMPasses
        LLVMX86CodeGen LLVMX86AsmParser LLVMX86Desc LLVMX86Info)
endif()

target_link_libraries(logos_jit PUBLIC ${LOGOS_JIT_LLVM_LIBS})

# Match logosc — LLVM is built without RTTI.
target_compile_options(logos_jit PRIVATE -fno-rtti)

# Smoke exerciser
add_executable(exerciser_jit_smoke exerciser_jit_smoke.cpp)
target_link_libraries(exerciser_jit_smoke PRIVATE logos_jit)
target_compile_options(exerciser_jit_smoke PRIVATE -fno-rtti)

# Tier audit for this directory's tests (cmake/LogosTestTiers.cmake): every
# test here must declare exactly one tier label, or the configure fails.
logos_require_tier_labels()

add_test(NAME jit_smoke COMMAND exerciser_jit_smoke)
set_tests_properties(jit_smoke PROPERTIES LABELS "jit;cpp;tier_full")

# Host-symbol resolution exerciser (slice 2).
add_executable(exerciser_jit_host_call exerciser_jit_host_call.cpp)
target_link_libraries(exerciser_jit_host_call PRIVATE logos_jit)
target_compile_options(exerciser_jit_host_call PRIVATE -fno-rtti)
# host_double must be visible as a dynamic symbol so JIT can resolve it
# even when we register it through ORC's absolute symbols (kept for safety
# in case future ORC paths fall back to process search).
set_target_properties(exerciser_jit_host_call PROPERTIES ENABLE_EXPORTS ON)
add_test(NAME jit_host_call COMMAND exerciser_jit_host_call)
set_tests_properties(jit_host_call PROPERTIES LABELS "jit;cpp;tier_full")
