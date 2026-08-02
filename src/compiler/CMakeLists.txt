# Copyright 2026 Victor Smirnov
# Logos project — https://github.com/victor-smirnov/logos
#
# logosc — Logos compiler (iteration 1).
#
# Pipeline: .logos source → PEG parser → Writ AST → MLIR → LLVM IR → native.
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
set(LOGOS_GRAMMAR ${CMAKE_SOURCE_DIR}/tools/peg_gen_cpp/grammars/logos.peg)
set(LOGOS_PARSER_HPP ${CMAKE_CURRENT_BINARY_DIR}/logos_parser.hpp)
set(LOGOS_PARSER_CPP ${CMAKE_CURRENT_BINARY_DIR}/logos_parser.cpp)

add_custom_command(
    OUTPUT  ${LOGOS_PARSER_HPP} ${LOGOS_PARSER_CPP}
    COMMAND $<TARGET_FILE:peg_gen_cpp>
            ${LOGOS_GRAMMAR}
            --out-dir ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS peg_gen_cpp ${LOGOS_GRAMMAR}
    COMMENT "Generating logos_parser from logos.peg"
)

# ---------------------------------------------------------------------------
# The WQL surface + EL expression parsers, generated from the SAME grammars the
# Logos backend generates the RUNTIME parsers from — one dialect, two backends.
# The compiler needs them to parse deem!/mapping rule bodies itself instead of
# handing raw text to a stdlib metaprogram.
#
# One invocation covers both: wql.peg `%import`s el.peg, and peg_gen_cpp emits a
# file pair per resolved module into --out-dir (imports first). The generated
# wql_surface_parser.hpp includes "el_parser.hpp" — same directory, already on
# logosc's include path.
#
# These TUs are self-contained: they reference logos::writ and nothing else. In
# particular they never call SEXPRARR_MAX()/RQAGG_MAX() — the fan caps are
# grammar-time literals in the .peg, which is exactly why linking is free.
set(WQL_GRAMMAR    ${CMAKE_SOURCE_DIR}/stdlib/mem/wql/grammars/wql.peg)
set(EL_GRAMMAR     ${CMAKE_SOURCE_DIR}/stdlib/mem/wql/grammars/el.peg)
set(WQL_PARSER_HPP ${CMAKE_CURRENT_BINARY_DIR}/wql_surface_parser.hpp)
set(WQL_PARSER_CPP ${CMAKE_CURRENT_BINARY_DIR}/wql_surface_parser.cpp)
set(EL_PARSER_HPP  ${CMAKE_CURRENT_BINARY_DIR}/el_parser.hpp)
set(EL_PARSER_CPP  ${CMAKE_CURRENT_BINARY_DIR}/el_parser.cpp)

add_custom_command(
    OUTPUT  ${WQL_PARSER_HPP} ${WQL_PARSER_CPP} ${EL_PARSER_HPP} ${EL_PARSER_CPP}
    COMMAND $<TARGET_FILE:peg_gen_cpp>
            ${WQL_GRAMMAR}
            --out-dir ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS peg_gen_cpp ${WQL_GRAMMAR} ${EL_GRAMMAR}
    COMMENT "Generating wql_surface_parser + el_parser from wql.peg (imports el.peg)"
)

# Generated parsers declare positional token captures that structural
# punctuation never reads.
set_source_files_properties(${WQL_PARSER_CPP} ${EL_PARSER_CPP} PROPERTIES
    COMPILE_OPTIONS "-Wno-unused-variable;-Wno-unused-but-set-variable")

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
    mlir_gen_debug.cpp
    compile_pipeline.cpp
    emit_module.cpp
    module_loader.cpp
    module_manifest.cpp
    lir_mirror.cpp
    lir_builder.cpp
    layout_law.cpp
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
    rule_ir.cpp
    ${LOGOS_PARSER_CPP}
    ${WQL_PARSER_CPP}
    ${EL_PARSER_CPP}
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
)
# SYSTEM: third-party LLVM/MLIR headers — silence their internal deprecations
# (e.g. AlignedCharArrayUnion in AlignOf.h, pulled in via llvm::Expected<T>).
target_include_directories(logosc SYSTEM PRIVATE
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
    logos_writ
    logos_jit
    ${LOGOS_MLIR_LLVM_LIBS}
)

# Note: MLIR/LLVM built without RTTI, but Writ requires it.
# We compile with RTTI enabled — MLIR's public API doesn't use RTTI
# (no dynamic_cast across boundary).  If linking fails, we'll need
# to split into separate TUs.
target_compile_options(logosc PRIVATE -fno-rtti)

# Place logosc in build/bin so it can find the system module library at
# build/lib/logos via argv[0]-relative resolution (../lib/logos), matching
# the install-tree layout.
set_target_properties(logosc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Keep an RPATH to the LLVM/MLIR lib dir in BOTH the build-tree and the
# INSTALLED binary, and only when that dir is non-standard. On Debian the
# distro's libMLIR.so lives under the private /usr/lib/llvm-NN/lib (not a
# default loader path), so without this logosc fails at startup with
# "libMLIR.so.NN: cannot open shared object file". On Fedora the libs are on
# the standard /usr/lib64 — the RPATH is both unnecessary AND trips rpmbuild's
# check-rpaths QA gate, so skip it for standard loader paths.
#
# BUILD_RPATH is not a nicety: the build ITSELF runs build/bin/logosc to compile
# the stdlib layers (`--emit-module`), so a build-tree binary the loader cannot
# start stops the build at the first stdlib layer — measured on a box whose
# LLVM came from apt.llvm.org rather than the distro. Setting INSTALL_RPATH
# alone made the condition right and its scope wrong: the run that needs the
# path first is the one inside the build.
if (LLVM_LIBRARY_DIR AND NOT LLVM_LIBRARY_DIR MATCHES "^(/usr/lib|/usr/lib64|/lib|/lib64)$")
    set_target_properties(logosc PROPERTIES
        BUILD_RPATH   "${LLVM_LIBRARY_DIR}"
        INSTALL_RPATH "${LLVM_LIBRARY_DIR}")
endif()

# Bake the bin-relative system library path into the binary. Resolving
# /proc/self/exe at runtime + appending this gives both the build-tree
# and install-tree directories with the same compiled-in suffix. The
# resolver tries `<reldir>/<SLOT>` first (versioned install tree) then
# `<reldir>` (flat build tree), so one binary serves both layouts.
target_compile_definitions(logosc PRIVATE
    LOGOS_LIB_RELDIR="../${LOGOS_LIB_RELDIR}"
    LOGOS_VERSION_FULL="${LOGOS_VERSION_FULL}"
    LOGOS_VERSION_SLOT="${LOGOS_VERSION_SLOT}")

