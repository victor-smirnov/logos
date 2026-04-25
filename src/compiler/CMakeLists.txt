# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Victor Smirnov
# Logos project — https://github.com/victor-smirnov/logos
#
# logosc — Logos compiler (iteration 1).
#
# Pipeline: .logos source → PEG parser → Hermes AST → MLIR → LLVM IR → native.
# Requires MLIR/LLVM dev libraries from the Jenny toolchain.

set(LOGOS_LLVM_DIR /opt/jenny-21x/lib/cmake/llvm CACHE PATH "LLVM cmake dir")
set(LOGOS_MLIR_DIR /opt/jenny-21x/lib/cmake/mlir CACHE PATH "MLIR cmake dir")

find_package(LLVM CONFIG QUIET HINTS ${LOGOS_LLVM_DIR})
find_package(MLIR CONFIG QUIET HINTS ${LOGOS_MLIR_DIR})

if (NOT LLVM_FOUND OR NOT MLIR_FOUND)
    message(STATUS "logosc: LLVM/MLIR dev libs not found — skipping compiler build")
    return()
endif()

message(STATUS "logosc: building with LLVM ${LLVM_PACKAGE_VERSION}")

# Include MLIR cmake helpers (provides mlir_tablegen, etc.)
list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}")
include(AddMLIR)

# ---------------------------------------------------------------------------
# Generate Logos parser from PEG grammar
# ---------------------------------------------------------------------------
set(LOGOS_GRAMMAR ${CMAKE_SOURCE_DIR}/tools/peg_gen/grammars/logos.peg)
set(LOGOS_PARSER_HPP ${CMAKE_CURRENT_BINARY_DIR}/logos_parser.hpp)
set(LOGOS_PARSER_CPP ${CMAKE_CURRENT_BINARY_DIR}/logos_parser.cpp)

add_custom_command(
    OUTPUT  ${LOGOS_PARSER_HPP} ${LOGOS_PARSER_CPP}
    COMMAND $<TARGET_FILE:peg_gen>
            ${LOGOS_GRAMMAR}
            --out-dir ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS peg_gen ${LOGOS_GRAMMAR}
    COMMENT "Generating logos_parser from logos.peg"
)

# ---------------------------------------------------------------------------
# logosc executable
# ---------------------------------------------------------------------------
add_executable(logosc
    main.cpp
    borrow_check.cpp
    mlir_gen.cpp
    mlir_gen_types.cpp
    mlir_gen_fn.cpp
    mlir_gen_stmt.cpp
    mlir_gen_expr.cpp
    mlir_gen_dyn.cpp
    emit_module.cpp
    module_loader.cpp
    module_manifest.cpp
    mono.cpp
    mono_subst.cpp
    mono_clone.cpp
    mono_scan.cpp
    reflection_emit.cpp
    sema.cpp
    sema_collect.cpp
    sema_auto_trait.cpp
    sema_expr.cpp
    sema_stmt.cpp
    sema_decl.cpp
    ${LOGOS_PARSER_CPP}
)

target_include_directories(logosc PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}   # generated logos_parser.hpp
    ${LLVM_INCLUDE_DIRS}
    ${MLIR_INCLUDE_DIRS}
)

# MLIR/LLVM libraries for the pipeline:
#   func dialect → arith dialect → scf/cf → LLVM dialect → LLVM IR → target
target_link_libraries(logosc PRIVATE
    logos_hermes
    logos_jit

    # MLIR dialects
    MLIRFuncDialect
    MLIRArithDialect
    MLIRSCFDialect
    MLIRControlFlowDialect
    MLIRLLVMDialect

    # MLIR → LLVM lowering passes
    MLIRFuncToLLVM
    MLIRArithToLLVM
    MLIRSCFToControlFlow
    MLIRControlFlowToLLVM
    MLIRReconcileUnrealizedCasts

    # MLIR infrastructure
    MLIRIR
    MLIRPass
    MLIRTransforms
    MLIRSupport

    # MLIR → LLVM IR translation
    MLIRTargetLLVMIRExport
    MLIRTargetLLVM
    MLIRBuiltinToLLVMIRTranslation
    MLIRLLVMToLLVMIRTranslation

    # LLVM backend (x86)
    LLVMX86CodeGen
    LLVMX86AsmParser
    LLVMX86Desc
    LLVMX86Info

    # LLVM core
    LLVMCore
    LLVMSupport
    LLVMTarget
    LLVMTargetParser
    LLVMMC
    LLVMAsmPrinter
    LLVMCodeGen
    LLVMAnalysis
    LLVMTransformUtils
    LLVMScalarOpts
    LLVMipo
    LLVMPasses
    LLVMIRReader
    LLVMAsmParser
    LLVMBitReader
)

# Note: MLIR/LLVM built without RTTI, but Hermes requires it.
# We compile with RTTI enabled — MLIR's public API doesn't use RTTI
# (no dynamic_cast across boundary).  If linking fails, we'll need
# to split into separate TUs.
target_compile_options(logosc PRIVATE -fno-rtti)

# Phase 4 slice 3: end-to-end JIT smoke — compiles a tiny .logos with
# logosc --jit and asserts main() returns 42 (the wrapper echoes "OK"
# on the expected exit code).
add_test(NAME jit_logosc_main
    COMMAND sh -c "\"$1\" --jit \"$2\"; test \$? -eq 42 && echo JIT_OK"
            sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/test_jit_main.logos)
set_tests_properties(jit_logosc_main PROPERTIES
    PASS_REGULAR_EXPRESSION "JIT_OK")

# Phase 4 slice 4 + Phase 7 slice 1: drive a #[metaprogram_post_sema]
# hook through the JIT during compilation. Hook calls the host emit
# marker; host dedup converges the loop after iter 1. Trace output
# must contain "metaprog iter 1" — proves the seam fired, the loop
# re-ran sema, and convergence kicked in.
add_test(NAME jit_metaprog_hook
    COMMAND sh -c "out=$(LOGOS_TRACE_PHASES=1 \"$1\" \"$2\" -I \"$4\" -o \"$3\" 2>&1); echo \"$out\"; echo \"$out\" | grep -q 'metaprog iter 1' && nm \"$3\" | grep -q ast_walk_ok && nm \"$3\" | grep -q fn_count"
            sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/test_jit_hook.logos
            ${CMAKE_CURRENT_BINARY_DIR}/test_jit_hook.o
            ${CMAKE_SOURCE_DIR}/stdlib)

# Phase 7 slice 6: derive-Debug-style metaprogram. Hook walks the
# user's AST, finds STRUCT nodes, reads each one's NAME and FIELDS
# count, and emits `pub fn debug_<NAME>_field_count() -> i64 ...`.
# Test fixture has Point (2 fields), RGB (3 fields), Marker (0
# fields, must be skipped). ctest verifies the right symbols
# appear and that the empty-struct skip works.
add_test(NAME jit_metaprog_derive
    COMMAND sh -c "out=$(LOGOS_TRACE_PHASES=1 \"$1\" \"$2\" -I \"$4\" -o \"$3\" 2>&1); echo \"$out\"; nm \"$3\" | grep -q debug_Point_field_count && nm \"$3\" | grep -q debug_RGB_field_count && ! nm \"$3\" | grep -q debug_Marker_field_count"
            sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/test_jit_derive.logos
            ${CMAKE_CURRENT_BINARY_DIR}/test_jit_derive.o
            ${CMAKE_SOURCE_DIR}/stdlib)
