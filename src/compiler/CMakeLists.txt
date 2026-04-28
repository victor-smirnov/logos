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
    lir_mirror.cpp
    lir_builder.cpp
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
    ctfe.cpp
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

# Phase 7 slice 13: spanned diagnostics. Handler emits via
# error_at(target_offset, msg); host prefixes file:line from the
# target node's SRC_LINE.
add_test(NAME jit_metaprog_diag_span
    COMMAND sh -c "out=$(\"$1\" \"$2\" -I \"$3\" -o /dev/null 2>&1); ec=$?; echo \"$out\"; test $ec -ne 0 && echo \"$out\" | grep -q ':16: metaprog hook .reject_hook.: rejected'"
            sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/test_jit_diag_span.logos
            ${CMAKE_SOURCE_DIR}/stdlib)

# Phase 7 slice 12: target-driven hook. #[metaprog_handler("derive_marker")]
# fn fires once per #[derive_marker]-annotated item in the user AST,
# receiving the item's offset. Hook synthesises `marker_<NAME>()` per
# triggered struct. Test verifies marker_Alpha and marker_Bravo emit.
add_test(NAME jit_metaprog_handler
    COMMAND sh -c "out=$(\"$1\" \"$2\" -I \"$4\" -o \"$3\" 2>&1); echo \"$out\"; nm \"$3\" | grep -q marker_Alpha && nm \"$3\" | grep -q marker_Bravo"
            sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/test_jit_handler.logos
            ${CMAKE_CURRENT_BINARY_DIR}/test_jit_handler.o
            ${CMAKE_SOURCE_DIR}/stdlib)

# Phase 7 slice 16: hook emits an `impl Debug for Widget { fn dbg ... }`
# block via Emitter::emit_raw. Test asserts `Widget__dbg` appears in nm —
# i.e. the impl was parsed, sema'd, mono'd, and lowered to LLVM.
add_test(NAME jit_metaprog_derive_debug
    COMMAND sh -c "out=$(\"$1\" \"$2\" -I \"$4\" -o \"$3\" 2>&1); echo \"$out\"; nm \"$3\" | grep -q Widget__dbg"
            sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/test_jit_derive_debug.logos
            ${CMAKE_CURRENT_BINARY_DIR}/test_jit_derive_debug.o
            ${CMAKE_SOURCE_DIR}/stdlib)

# Phase 7 slice 14: multiple handlers per trigger. Both pair_a_hook and
# pair_b_hook are registered for "derive_pair" and must both fire on
# the single #[derive_pair] struct Solo.
add_test(NAME jit_metaprog_handler_multi
    COMMAND sh -c "out=$(\"$1\" \"$2\" -I \"$4\" -o \"$3\" 2>&1); echo \"$out\"; nm \"$3\" | grep -q pair_a_Solo && nm \"$3\" | grep -q pair_b_Solo"
            sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/test_jit_handler_multi.logos
            ${CMAKE_CURRENT_BINARY_DIR}/test_jit_handler_multi.o
            ${CMAKE_SOURCE_DIR}/stdlib)

