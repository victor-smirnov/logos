// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
//
// emit_module — build a binary Logos module (.a archive) from a manifest.
//
// Output: libNAME.a containing
//   NAME.o       — compiled non-generic code for the whole module
//   NAME.hermes0 — binary AST dump (for sema on client side)

#include "emit_module.hpp"
#include "metaprog_dispatch.hpp"
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
        if (std::getenv("LOGOS_DUMP_MLIR_ON_FAIL")) {
            mlir_module->dump();
        }
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
// Filter codegen body emission to one source file when `only_file` is set:
// every fn whose source_file != only_file is added to prog.binary_symbols
// so mlir_gen forward-declares it without emitting a body. The linker
// resolves these symbols at archive-merge time from sibling per-file .o
// files produced by other --emit-file invocations.
static void apply_only_file_filter(lir::LProgram& prog,
                                    const std::string& only_file) {
    if (only_file.empty()) return;
    auto fits = [&](const std::string& src) {
        if (src == only_file) return true;
        // Tolerate path-shape mismatches by also matching on the lexical
        // suffix (e.g. relative vs canonical paths).
        if (src.size() >= only_file.size() &&
            src.compare(src.size() - only_file.size(), only_file.size(), only_file) == 0)
            return true;
        if (only_file.size() >= src.size() &&
            only_file.compare(only_file.size() - src.size(), src.size(), src) == 0)
            return true;
        return false;
    };
    auto add = [&](const lir::LFunction& fn) {
        if (fn.is_extern) return;
        if (fits(fn.source_file)) return;
        prog.binary_symbols.insert(fn.name);
    };
    for (auto& sd : prog.structs)
        for (auto& m : sd.methods) add(*m);
    for (auto& fn : prog.functions) add(*fn);
}

static bool compile_to_object(std::vector<hermes::Hermes>& asts,
                               std::vector<std::string>& filenames,
                               const std::vector<bool>& ast_only_flags,
                               const std::string& obj_path,
                               const std::string& only_file = "") {
    // Run metaprog discovery loop (#21 closure) so #[derive_*] hooks
    // and metacall thunks fire during stdlib build. asts/filenames
    // grow with synthesised docs that subsequent sema picks up.
    //
    // ast_only modules participate in dispatch (their handler fns
    // need to JIT-compile + register triggers) but get from_binary=
    // true for the post-dispatch sema pass — host externs in their
    // bodies (logos_emit_*, etc.) make them unsuitable for codegen.
    std::vector<bool> from_binary(asts.size(), false);
    {
        MetaprogDispatchOpts mopts;
        // No archive paths in stdlib build (we ARE the archive being built).
        // No --dump-metaprog wiring here yet — separate slice if needed.
        size_t entry_idx = asts.empty() ? 0 : asts.size() - 1;
        if (run_metaprog_dispatch(asts, filenames, from_binary, entry_idx, mopts) != 0)
            return false;
    }
    // Re-stamp from_binary: ast_only files become "binary" so the
    // post-dispatch sema/mono/mlir-gen pass treats them as already-
    // emitted (no codegen for fns that bind to host externs). Synth
    // docs appended by dispatch (filename "<metaprog-blob-subst>" /
    // "<metaprog>") stay non-binary so their items get lowered.
    from_binary.assign(asts.size(), false);
    for (size_t i = 0; i < ast_only_flags.size() && i < from_binary.size(); ++i)
        from_binary[i] = ast_only_flags[i];
    // Sema — all files in the module are being compiled from source.
    auto prog = sema_lower(asts, filenames, from_binary);
    prog.print_diags(stderr);
    if (!prog.ok()) return false;

    // Mono (also emits L-IR Hermes mirror; borrow_check reads via mirror)
    prog = mono_pass(std::move(prog));
    prog.print_diags(stderr);
    if (!prog.ok()) return false;

    // Borrow check
    prog = borrow_check(std::move(prog));
    prog.print_diags(stderr);
    if (!prog.ok()) return false;

    // B1.7: per-file mode marks every fn outside the target file as
    // binary-skip so mlir_gen forward-declares without bodies.
    apply_only_file_filter(prog, only_file);

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
    // Per-function & per-data sections so downstream `--gc-sections`
    // strips unused stdlib symbols at link time. Without this, the entire
    // stdlib.o gets pulled into any binary that references *any* symbol
    // in it (single .o → all-or-nothing archive extraction).
    llvm::TargetOptions tmopts;
    tmopts.FunctionSections = true;
    tmopts.DataSections     = true;
    auto tm = std::unique_ptr<llvm::TargetMachine>(
        target->createTargetMachine(
            llvm_module->getTargetTriple(), "generic", "",
            tmopts, llvm::Reloc::PIC_));
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

    auto absolutize_prefixes = [&](const std::vector<std::string>& src) {
        std::vector<std::string> out;
        out.reserve(src.size());
        for (auto& e : src) {
            auto p = fs::path(root) / e;
            out.push_back(fs::weakly_canonical(p).string());
        }
        return out;
    };
    auto matches_any = [](const std::string& f, const std::vector<std::string>& prefixes) {
        for (auto& p : prefixes)
            if (f.compare(0, p.size(), p) == 0) return true;
        return false;
    };

    auto abs_excludes = absolutize_prefixes(manifest.excludes);
    auto abs_ast_only = absolutize_prefixes(manifest.ast_only);

    // Collect all .logos files under the module root and bucket them:
    //   exclude  → drop entirely (not in archive at all).
    //   ast_only → loaded for AST emission, skipped at codegen (host-only
    //     externs make the .o invalid for user-link-time use; the AST
    //     is still needed by the metacall JIT at compile time).
    //   regular  → both .o and .hermes0.
    auto all_files = collect_logos_files(root);
    std::vector<std::string> codegen_files;
    std::vector<std::string> ast_only_files;
    codegen_files.reserve(all_files.size());
    for (auto& f : all_files) {
        if (matches_any(f, abs_excludes)) continue;
        if (matches_any(f, abs_ast_only)) ast_only_files.push_back(f);
        else                              codegen_files.push_back(f);
    }
    if (codegen_files.empty() && ast_only_files.empty()) {
        std::fprintf(stderr, "emit_module: no .logos files found under %s\n",
                     root.c_str());
        return false;
    }
    if (verbose) {
        std::fprintf(stderr, "emit_module: found %zu codegen file(s), %zu ast-only file(s)\n",
                     codegen_files.size(), ast_only_files.size());
    }

    // Parse all files. load_modules follows `use`-deps transitively from
    // each entry; we load both buckets, dedup by path. ast_only files
    // load LAST so transitive deps reachable from codegen files surface
    // in the codegen bucket first.
    std::vector<ParsedModule> modules;
    {
        std::unordered_set<std::string> seen;
        auto load_bucket = [&](const std::vector<std::string>& bucket) {
            for (auto& file : bucket) {
                auto mods = load_modules(file, search_paths, nullptr,
                                         opts.extra_lib_files);
                for (auto& m : mods) {
                    if (seen.insert(m.path).second)
                        modules.push_back(std::move(m));
                }
            }
        };
        load_bucket(codegen_files);
        load_bucket(ast_only_files);
    }

    if (modules.empty()) {
        std::fprintf(stderr, "emit_module: failed to load any modules\n");
        return false;
    }
    if (verbose) {
        std::fprintf(stderr, "emit_module: loaded %zu module(s) total\n", modules.size());
    }

    auto is_ast_only_path = [&](const std::string& path) {
        return matches_any(path, abs_ast_only);
    };

    // Resolve absolute path of the per-file target if --only-file is set.
    // load_modules canonicalizes paths, so match against the same form.
    std::string only_file_canon;
    if (!opts.only_file.empty()) {
        std::error_code can_ec;
        only_file_canon = fs::weakly_canonical(opts.only_file, can_ec).string();
        if (can_ec) only_file_canon = opts.only_file;
    }

    // Output paths. In per-file mode, write `<output_path>.o` and
    // `<output_path>.hermes0` directly (no archive). In standard mode,
    // intermediate files go in a temp dir and `ar` builds the .a.
    std::string obj_path;
    std::string h0_path;
    if (!opts.only_file.empty()) {
        obj_path = output_path + ".o";
        h0_path  = output_path + ".hermes0";
    } else {
        auto tmp_dir = fs::temp_directory_path() / ("logos_emit_" + manifest.name);
        std::error_code ec;
        fs::create_directories(tmp_dir, ec);
        obj_path = (tmp_dir / (manifest.name + ".o")).string();
        h0_path  = (tmp_dir / (manifest.name + ".hermes0")).string();
    }

    // Build AST + filename arrays for codegen — skip ast_only modules.
    // .hermes0 takes everything (incl. ast_only).
    //
    // Compile-to-object also needs ast_only modules in its asts vector
    // — the metaprog dispatch loop (#21 closure) JIT-compiles their
    // handler fn bodies + reads their `#[metaprog_handler]` triggers.
    // We stamp ast_only with from_binary=true after dispatch so sema's
    // post-dispatch pass treats those items as already-emitted (no
    // codegen for host-extern-using fns).
    std::vector<hermes::Hermes> asts;
    std::vector<std::string> filenames;
    std::vector<bool>        ast_only_flags;   // parallel to asts
    std::vector<ParsedModule> modules_for_h0;
    for (auto& m : modules) {
        modules_for_h0.push_back({m.path, m.package, m.ast});  // Hermes is copy-on-write safe
        bool ao = is_ast_only_path(m.path);
        filenames.push_back(m.path);
        asts.push_back(std::move(m.ast));
        ast_only_flags.push_back(ao);
    }

    // Compile to object file.
    if (verbose) {
        std::fprintf(stderr, "emit_module: compiling %zu file(s)%s%s%s → %s\n",
                     filenames.size(),
                     only_file_canon.empty() ? "" : " (filtering to ",
                     only_file_canon.empty() ? "" : only_file_canon.c_str(),
                     only_file_canon.empty() ? "" : ")",
                     obj_path.c_str());
    }
    if (!compile_to_object(asts, filenames, ast_only_flags, obj_path, only_file_canon)) {
        std::fprintf(stderr, "emit_module: compilation failed\n");
        return false;
    }

    // .hermes0: in per-file mode, contains only the target file's AST.
    if (!opts.only_file.empty()) {
        std::vector<ParsedModule> single;
        for (auto& m : modules_for_h0) {
            std::error_code can_ec;
            auto canon = fs::weakly_canonical(m.path, can_ec).string();
            if (can_ec) canon = m.path;
            if (canon == only_file_canon ||
                (canon.size() >= only_file_canon.size() &&
                 canon.compare(canon.size() - only_file_canon.size(),
                               only_file_canon.size(), only_file_canon) == 0)) {
                single.push_back(m);
                break;
            }
        }
        if (single.empty()) {
            std::fprintf(stderr,
                "emit_module: --only-file '%s' did not match any source file in the manifest\n",
                opts.only_file.c_str());
            return false;
        }
        if (verbose) {
            std::fprintf(stderr, "emit_module: writing → %s (single file)\n", h0_path.c_str());
        }
        if (!write_hermes0(h0_path, single)) {
            std::fprintf(stderr, "emit_module: .hermes0 write failed\n");
            return false;
        }
        // Per-file mode also wraps .hermes0 → .hermes0.o so lforge can
        // archive it without ld.lld emitting an "is neither ET_REL nor
        // LLVM bitcode" warning at downstream link time.
        // Match emit_module-mode naming: "<base>.hm0" (≤15 chars in ar).
        std::string h0_obj =
            h0_path.substr(0, h0_path.find_last_of('.')) + ".hm0";
        {
            std::ostringstream cmd;
            cmd << "objcopy -I binary -O elf64-x86-64 "
                << "--rename-section .data=.lhermes "
                << h0_path << " " << h0_obj;
            if (verbose) {
                std::fprintf(stderr, "emit_module: %s\n", cmd.str().c_str());
            }
            if (std::system(cmd.str().c_str()) != 0) {
                std::fprintf(stderr, "emit_module: objcopy wrap failed\n");
                return false;
            }
        }
        if (verbose) {
            std::fprintf(stderr, "emit_module: wrote %s + %s\n",
                         obj_path.c_str(), h0_obj.c_str());
        }
        return true;
    }

    if (verbose) {
        std::fprintf(stderr, "emit_module: writing → %s\n", h0_path.c_str());
    }
    if (!write_hermes0(h0_path, modules_for_h0)) {
        std::fprintf(stderr, "emit_module: .hermes0 write failed\n");
        return false;
    }

    // Wrap .hermes0 as a relocatable ELF object so ld.lld doesn't warn
    // about a non-ET_REL archive member when downstream binaries link
    // against this archive. The data lives in a non-ALLOC `.lhermes`
    // section; module_loader looks for that section by name.
    // Wrap into "<basename>.hm0" (must stay <=15 chars including the
    // trailing `/` separator that ar appends, otherwise the name spills
    // into the GNU extended name table — which our ar parser doesn't
    // chase).
    std::string h0_obj_path =
        h0_path.substr(0, h0_path.find_last_of('.')) + ".hm0";
    {
        std::ostringstream cmd;
        cmd << "objcopy -I binary -O elf64-x86-64 "
            << "--rename-section .data=.lhermes "
            << h0_path << " " << h0_obj_path;
        if (verbose) {
            std::fprintf(stderr, "emit_module: %s\n", cmd.str().c_str());
        }
        if (std::system(cmd.str().c_str()) != 0) {
            std::fprintf(stderr, "emit_module: objcopy wrap failed\n");
            return false;
        }
    }

    // Create .a archive: ar rcs output.a NAME.o NAME.hermes0.o
    {
        std::ostringstream cmd;
        cmd << "ar rcs " << output_path
            << " " << obj_path
            << " " << h0_obj_path;
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
