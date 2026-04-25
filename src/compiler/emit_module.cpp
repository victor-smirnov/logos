// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
//
// emit_module — build a binary Logos module (.a archive) from a manifest.
//
// Output: libNAME.a containing
//   NAME.o       — compiled non-generic code for the whole module
//   NAME.hermes0 — binary AST dump (for sema on client side)

#include "emit_module.hpp"
#include "module_loader.hpp"
#include "mlir_gen.hpp"

#include <logos/compiler/borrow_check.hpp>
#include <logos/compiler/lir.hpp>
#include <logos/compiler/mono.hpp>
#include <logos/hermes/binary_codec.hpp>
#include <logos/hermes/document.hpp>
#include <logos/hermes/type_ops.hpp>

// MLIR
#include <mlir/IR/MLIRContext.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/Dialect/ControlFlow/IR/ControlFlowOps.h>
#include <mlir/Dialect/LLVMIR/LLVMDialect.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Pass/PassManager.h>
#include <mlir/Conversion/FuncToLLVM/ConvertFuncToLLVMPass.h>
#include <mlir/Conversion/ArithToLLVM/ArithToLLVM.h>
#include <mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h>
#include <mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h>
#include <mlir/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.h>
#include <mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h>
#include <mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h>
#include <mlir/Target/LLVMIR/Export.h>

// LLVM
#include <llvm/IR/Module.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace logos::compiler {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// .hermes0 format (version 2)
//
//   magic[8]      "HERMAST0"
//   version       uint32_t  = 2
//   num_files     uint32_t
//   for each file:
//     path_len    uint32_t
//     path        char[path_len]
//     pkg_len     uint32_t
//     pkg         char[pkg_len]   — dotted package name (e.g. "std.io")
//     ast_len     uint64_t
//     ast         uint8_t[ast_len]  (binary_codec output)
// ---------------------------------------------------------------------------

static void write_u32(std::ofstream& f, uint32_t v) {
    f.write(reinterpret_cast<const char*>(&v), 4);
}
static void write_u64(std::ofstream& f, uint64_t v) {
    f.write(reinterpret_cast<const char*>(&v), 8);
}

static bool write_hermes0(const std::string& path,
                           const std::vector<ParsedModule>& modules) {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "emit_module: cannot create %s\n", path.c_str());
        return false;
    }
    // header
    f.write("HERMAST0", 8);
    write_u32(f, 2);  // version 2: adds pkg_len+pkg per file
    write_u32(f, static_cast<uint32_t>(modules.size()));

    for (auto& mod : modules) {
        auto enc = hermes::binary_encode(mod.ast);
        if (!enc) {
            std::fprintf(stderr, "emit_module: binary_encode failed for %s\n",
                         mod.path.c_str());
            return false;
        }
        uint32_t path_len = static_cast<uint32_t>(mod.path.size());
        write_u32(f, path_len);
        f.write(mod.path.data(), path_len);
        uint32_t pkg_len = static_cast<uint32_t>(mod.package.size());
        write_u32(f, pkg_len);
        f.write(mod.package.data(), pkg_len);
        uint64_t ast_len = enc->size();
        write_u64(f, ast_len);
        f.write(reinterpret_cast<const char*>(enc->data()), ast_len);
    }
    return f.good();
}

// ---------------------------------------------------------------------------
// Collect all .logos files under a directory (recursive).
// ---------------------------------------------------------------------------
static std::vector<std::string> collect_logos_files(const std::string& root) {
    std::vector<std::string> result;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(
             root, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator();
         it.increment(ec))
    {
        if (!it->is_regular_file(ec)) continue;
        if (it->path().extension() != ".logos") continue;
        result.push_back(fs::weakly_canonical(it->path(), ec).string());
    }
    std::sort(result.begin(), result.end());
    return result;
}

// ---------------------------------------------------------------------------
// Lower an already-typechecked LProgram to an LLVM module.
// Public entry — used by emit_module (→ .o) and by --jit (→ in-process).
// ---------------------------------------------------------------------------
std::unique_ptr<llvm::Module>
lower_to_llvm_module(const lir::LProgram& prog, llvm::LLVMContext& llvm_ctx) {
    mlir::MLIRContext mlir_ctx;
    mlir_ctx.getOrLoadDialect<mlir::func::FuncDialect>();
    mlir_ctx.getOrLoadDialect<mlir::arith::ArithDialect>();
    mlir_ctx.getOrLoadDialect<mlir::scf::SCFDialect>();
    mlir_ctx.getOrLoadDialect<mlir::cf::ControlFlowDialect>();
    mlir_ctx.getOrLoadDialect<mlir::LLVM::LLVMDialect>();

    auto mlir_module = mlir_gen(mlir_ctx, prog);
    if (!mlir_module) {
        std::fprintf(stderr, "lower_to_llvm_module: MLIR generation failed\n");
        return nullptr;
    }

    mlir::PassManager pm(&mlir_ctx);
    pm.addPass(mlir::createSCFToControlFlowPass());
    pm.addPass(mlir::createConvertControlFlowToLLVMPass());
    pm.addPass(mlir::createArithToLLVMConversionPass());
    pm.addPass(mlir::createConvertFuncToLLVMPass());
    pm.addPass(mlir::createReconcileUnrealizedCastsPass());
    if (mlir::failed(pm.run(*mlir_module))) {
        std::fprintf(stderr, "lower_to_llvm_module: MLIR lowering failed\n");
        return nullptr;
    }

    mlir::registerBuiltinDialectTranslation(mlir_ctx);
    mlir::registerLLVMDialectTranslation(mlir_ctx);
    auto llvm_module = mlir::translateModuleToLLVMIR(*mlir_module, llvm_ctx);
    if (!llvm_module) {
        std::fprintf(stderr, "lower_to_llvm_module: LLVM IR translation failed\n");
        return nullptr;
    }
    llvm_module->setTargetTriple(llvm::Triple(llvm::sys::getDefaultTargetTriple()));
    return llvm_module;
}

// ---------------------------------------------------------------------------
// Compile a set of parsed modules to an object file.
// Returns true on success.
// ---------------------------------------------------------------------------
static bool compile_to_object(const std::vector<hermes::Hermes>& asts,
                               const std::vector<std::string>& filenames,
                               const std::string& obj_path) {
    // Sema — all files in the module are being compiled from source (not binary)
    auto prog = sema_lower(asts, filenames, {});
    prog.print_diags(stderr);
    if (!prog.ok()) return false;

    // Mono
    prog = mono_pass(std::move(prog));
    prog.print_diags(stderr);
    if (!prog.ok()) return false;

    // Borrow check
    prog = borrow_check(std::move(prog));
    prog.print_diags(stderr);
    if (!prog.ok()) return false;

    llvm::LLVMContext llvm_ctx;
    auto llvm_module = lower_to_llvm_module(prog, llvm_ctx);
    if (!llvm_module) return false;

    // LLVM IR → .o
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    std::string err;
    auto* target = llvm::TargetRegistry::lookupTarget(
        llvm_module->getTargetTriple(), err);
    if (!target) {
        std::fprintf(stderr, "emit_module: target lookup: %s\n", err.c_str());
        return false;
    }
    auto tm = std::unique_ptr<llvm::TargetMachine>(
        target->createTargetMachine(
            llvm_module->getTargetTriple(), "generic", "",
            llvm::TargetOptions{}, llvm::Reloc::PIC_));
    llvm_module->setDataLayout(tm->createDataLayout());

    std::error_code ec;
    llvm::raw_fd_ostream out(obj_path, ec, llvm::sys::fs::OF_None);
    if (ec) {
        std::fprintf(stderr, "emit_module: cannot create %s: %s\n",
                     obj_path.c_str(), ec.message().c_str());
        return false;
    }
    llvm::legacy::PassManager pass;
    if (tm->addPassesToEmitFile(pass, out, nullptr,
                                llvm::CodeGenFileType::ObjectFile)) {
        std::fprintf(stderr, "emit_module: cannot emit object file\n");
        return false;
    }
    pass.run(*llvm_module);
    out.flush();
    return true;
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------
bool emit_module(const ModuleManifest& manifest,
                 const std::string& output_path,
                 const EmitModuleOptions& opts)
{
    // Progress output is noisy on every build; gate it behind LOGOS_EMIT_VERBOSE=1.
    bool verbose = false;
    if (const char* v = std::getenv("LOGOS_EMIT_VERBOSE")) {
        verbose = (v[0] != '\0' && v[0] != '0');
    }

    // Resolve root relative to cwd.
    auto root = fs::weakly_canonical(manifest.root).string();
    if (verbose) {
        std::fprintf(stderr, "emit_module: building module '%s' from %s\n",
                     manifest.name.c_str(), root.c_str());
    }

    // Build search paths: module root + extra paths from -I flags.
    std::vector<std::string> search_paths = {root};
    for (auto& p : opts.extra_search_paths) search_paths.push_back(p);

    // Collect all .logos files under the module root.
    auto all_files = collect_logos_files(root);
    if (all_files.empty()) {
        std::fprintf(stderr, "emit_module: no .logos files found under %s\n",
                     root.c_str());
        return false;
    }
    if (verbose) {
        std::fprintf(stderr, "emit_module: found %zu source file(s)\n", all_files.size());
    }

    // Parse all files.  Use load_modules on the first file with the root as
    // search path — this follows use-deps transitively, so all needed files
    // (including those outside root) are included.  Files discovered this way
    // but outside root are included for compilation but not re-exported in the
    // manifest (they're dependencies, not part of this module).
    // For a comprehensive scan of the root itself, we load every file.
    std::vector<ParsedModule> modules;
    {
        // Load starting from every file in the root so we get them all even if
        // they're not reachable from a single entry point.
        // We deduplicate by path using load_modules which tracks visited_files.
        // Simplest: just load the first file; module_loader follows all uses.
        // For modules without a single root file, load each file separately and
        // deduplicate.
        std::unordered_set<std::string> seen;
        for (auto& file : all_files) {
            auto mods = load_modules(file, search_paths);
            for (auto& m : mods) {
                if (seen.insert(m.path).second)
                    modules.push_back(std::move(m));
            }
        }
    }

    if (modules.empty()) {
        std::fprintf(stderr, "emit_module: failed to load any modules\n");
        return false;
    }
    if (verbose) {
        std::fprintf(stderr, "emit_module: loaded %zu module(s) total\n", modules.size());
    }

    // Prepare temp dir for intermediate files.
    auto tmp_dir = fs::temp_directory_path() / ("logos_emit_" + manifest.name);
    std::error_code ec;
    fs::create_directories(tmp_dir, ec);

    std::string obj_path  = (tmp_dir / (manifest.name + ".o")).string();
    std::string h0_path   = (tmp_dir / (manifest.name + ".hermes0")).string();

    // Collect ASTs + filenames for the compiler pipeline.
    std::vector<hermes::Hermes> asts;
    std::vector<std::string> filenames;
    // Keep a copy of modules for .hermes0 (asts are moved below).
    std::vector<ParsedModule> modules_for_h0;
    for (auto& m : modules) {
        filenames.push_back(m.path);
        modules_for_h0.push_back({m.path, m.package, m.ast});  // Hermes is copy-on-write safe
        asts.push_back(std::move(m.ast));
    }

    // Compile to object file.
    if (verbose) {
        std::fprintf(stderr, "emit_module: compiling → %s\n", obj_path.c_str());
    }
    if (!compile_to_object(asts, filenames, obj_path)) {
        std::fprintf(stderr, "emit_module: compilation failed\n");
        return false;
    }

    // Write .hermes0.
    if (verbose) {
        std::fprintf(stderr, "emit_module: writing → %s\n", h0_path.c_str());
    }
    if (!write_hermes0(h0_path, modules_for_h0)) {
        std::fprintf(stderr, "emit_module: .hermes0 write failed\n");
        return false;
    }

    // Create .a archive: ar rcs output.a NAME.o NAME.hermes0
    {
        std::ostringstream cmd;
        cmd << "ar rcs " << output_path
            << " " << obj_path
            << " " << h0_path;
        if (verbose) {
            std::fprintf(stderr, "emit_module: %s\n", cmd.str().c_str());
        }
        if (std::system(cmd.str().c_str()) != 0) {
            std::fprintf(stderr, "emit_module: ar failed\n");
            return false;
        }
    }

    if (verbose) {
        std::fprintf(stderr, "emit_module: wrote %s\n", output_path.c_str());
    }
    return true;
}

} // namespace logos::compiler
