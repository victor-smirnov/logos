# Copyright 2026 Victor Smirnov
# Logos project — https://github.com/victor-smirnov/logos
#
# logosc — Logos compiler (iteration 1).
#
# Pipeline: .logos source → PEG parser → Hermes AST → MLIR → LLVM IR → native.
# Requires MLIR/LLVM dev libraries from the Jenny toolchain.

# LOGOS_LLVM_DIR / LOGOS_MLIR_DIR are discovered (or -D-overridden) at the
# top-level CMakeLists; here we just consume them.
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
    region_infer.cpp
    mlir_gen.cpp
    mlir_gen_types.cpp
    mlir_gen_fn.cpp
    mlir_gen_stmt.cpp
    mlir_gen_expr.cpp
    mlir_gen_dyn.cpp
    compile_pipeline.cpp
    emit_module.cpp
    module_loader.cpp
    module_manifest.cpp
    lir_mirror.cpp
    lir_builder.cpp
    mono.cpp
    mono_subst.cpp
    mono_clone.cpp
    mono_scan.cpp
    mono_const_arg.cpp
    reflection_emit.cpp
    sema.cpp
    sema_collect.cpp
    sema_auto_trait.cpp
    sema_expr.cpp
    sema_fmt.cpp
    sema_render.cpp
    sema_stmt.cpp
    sema_decl.cpp
    trait_engine.cpp
    ctfe.cpp
    ${LOGOS_PARSER_CPP}
)

# ---------------------------------------------------------------------------
# trait_engine_test — Phase 1 unit tests for the trait resolver engine.
# Pure-data engine, no compiler integration; runs as a standalone bin.
# ---------------------------------------------------------------------------
add_executable(trait_engine_test
    trait_engine.cpp
    trait_engine_test.cpp
)
add_test(NAME trait_engine_test COMMAND trait_engine_test
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(trait_engine_test PROPERTIES LABELS "trait_engine;cpp")

# Verifies TypeRef/lir_view cross-arena dispatch (ExternalRef detection +
# ArenaPool resolution end-to-end). See docs/internals/multi-arena-ir.md.

target_include_directories(logosc PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}   # generated logos_parser.hpp
    ${LLVM_INCLUDE_DIRS}
    ${MLIR_INCLUDE_DIRS}
)

# MLIR/LLVM libraries for the pipeline:
#   func dialect → arith dialect → scf/cf → LLVM dialect → LLVM IR → target
#
# How we link LLVM/MLIR depends on how the toolchain was built:
#
#  * Distro-stock (Ubuntu llvm-20-dev): LLVM_LINK_LLVM_DYLIB is ON and the
#    distro's MLIR component archives are themselves built against the single
#    libLLVM.so. Linking the individual static LLVM component archives here
#    would pull a SECOND copy of LLVM's process-global state (cl::opt option
#    registry, ManagedStatic) next to that libLLVM.so — which manifested as
#    cl::opt strings constructed-from-null and a double-free at exit. So we
#    link the two aggregate dylibs (libMLIR.so + libLLVM.so), one copy each.
#
#  * Static toolchain (custom from-source LLVM with the dylib off): there is
#    no shared library to collide with, so we link the explicit component
#    archives.
if (LLVM_LINK_LLVM_DYLIB)
    set(LOGOS_MLIR_LLVM_LIBS MLIR LLVM)
else()
    set(LOGOS_MLIR_LLVM_LIBS
        # MLIR dialects
        MLIRFuncDialect MLIRArithDialect MLIRSCFDialect
        MLIRControlFlowDialect MLIRLLVMDialect
        # MLIR → LLVM lowering passes
        MLIRFuncToLLVM MLIRArithToLLVM MLIRSCFToControlFlow
        MLIRControlFlowToLLVM MLIRReconcileUnrealizedCasts
        # MLIR infrastructure
        MLIRIR MLIRPass MLIRTransforms MLIRSupport
        # MLIR → LLVM IR translation
        MLIRTargetLLVMIRExport MLIRTargetLLVM
        MLIRBuiltinToLLVMIRTranslation MLIRLLVMToLLVMIRTranslation
        # LLVM backend (x86)
        LLVMX86CodeGen LLVMX86AsmParser LLVMX86Desc LLVMX86Info
        # LLVM core
        LLVMCore LLVMSupport LLVMTarget LLVMTargetParser LLVMMC
        LLVMAsmPrinter LLVMCodeGen LLVMAnalysis LLVMTransformUtils
        LLVMScalarOpts LLVMipo LLVMPasses LLVMIRReader LLVMAsmParser
        LLVMBitReader)
endif()

target_link_libraries(logosc PRIVATE
    logos_hermes
    logos_jit
    ${LOGOS_MLIR_LLVM_LIBS}
)

# Note: MLIR/LLVM built without RTTI, but Hermes requires it.
# We compile with RTTI enabled — MLIR's public API doesn't use RTTI
# (no dynamic_cast across boundary).  If linking fails, we'll need
# to split into separate TUs.
target_compile_options(logosc PRIVATE -fno-rtti)

# Place logosc in build/bin so it can find the system module library at
# build/lib/logos via argv[0]-relative resolution (../lib/logos), matching
# the install-tree layout.
set_target_properties(logosc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Keep an RPATH to the LLVM/MLIR lib dir in the INSTALLED binary ONLY when that
# dir is non-standard. On Debian the distro's libMLIR.so lives under the
# private /usr/lib/llvm-NN/lib (not a default loader path), so without this the
# installed/packaged logosc fails at startup with "libMLIR.so.NN: cannot open
# shared object file". On Fedora the libs are on the standard /usr/lib64 — the
# RPATH is both unnecessary AND trips rpmbuild's check-rpaths QA gate, so skip
# it for standard loader paths.
if (LLVM_LIBRARY_DIR AND NOT LLVM_LIBRARY_DIR MATCHES "^(/usr/lib|/usr/lib64|/lib|/lib64)$")
    set_target_properties(logosc PROPERTIES
        INSTALL_RPATH "${LLVM_LIBRARY_DIR}")
endif()

# Bake the bin-relative system library path into the binary. Resolving
# /proc/self/exe at runtime + appending this gives both the build-tree
# and install-tree directories with the same compiled-in suffix.
target_compile_definitions(logosc PRIVATE
    LOGOS_LIB_RELDIR="../${LOGOS_LIB_RELDIR}")

