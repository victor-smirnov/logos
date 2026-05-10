// SPDX-License-Identifier: Apache-2.0
// Shared MLIR/LLVM lowering tail. See compile_pipeline.hpp for the contract.
//
// Body extracted verbatim from the duplicated chunks at the end of
// main.cpp's user pipeline and emit_module.cpp's compile_to_object.
#include "compile_pipeline.hpp"
#include "mlir_gen.hpp"

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

#include <llvm/IR/Module.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/PassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>

#include <cstdio>

namespace logos::compiler {

int lower_and_emit_object(lir::LProgram& prog,
                           const std::string& output_path,
                           const LowerEmitOpts& opts)
{
    // ── L-IR → MLIR ─────────────────────────────────────────────
    mlir::MLIRContext mlir_ctx;
    mlir_ctx.getOrLoadDialect<mlir::func::FuncDialect>();
    mlir_ctx.getOrLoadDialect<mlir::arith::ArithDialect>();
    mlir_ctx.getOrLoadDialect<mlir::scf::SCFDialect>();
    mlir_ctx.getOrLoadDialect<mlir::cf::ControlFlowDialect>();
    mlir_ctx.getOrLoadDialect<mlir::LLVM::LLVMDialect>();

    auto mlir_module = mlir_gen(mlir_ctx, prog);
    if (std::getenv("LOGOS_DUMP_MLIR")) mlir_module->dump();
    if (!mlir_module) {
        std::fprintf(stderr, "logosc: MLIR generation failed\n");
        return 1;
    }

    // --dump-metaprog phase 2: post-mono MLIR snapshot.
    if (!opts.dump_metaprog_dir.empty()) {
        std::string mlir_text;
        {
            llvm::raw_string_ostream os(mlir_text);
            mlir_module->print(os);
        }
        std::string path = opts.dump_metaprog_dir + "/_global_post_mono.mlir";
        if (FILE* f = std::fopen(path.c_str(), "w")) {
            std::fwrite(mlir_text.data(), 1, mlir_text.size(), f);
            std::fclose(f);
        }
    }

    if (opts.emit_mlir) { mlir_module->dump(); return 0; }

    // ── MLIR → LLVM dialect ────────────────────────────────────
    if (std::getenv("LOGOS_DUMP_MLIR")) mlir_module->dump();
    mlir::PassManager pm(&mlir_ctx);
    pm.addPass(mlir::createSCFToControlFlowPass());
    pm.addPass(mlir::createConvertControlFlowToLLVMPass());
    pm.addPass(mlir::createArithToLLVMConversionPass());
    pm.addPass(mlir::createConvertFuncToLLVMPass());
    pm.addPass(mlir::createReconcileUnrealizedCastsPass());
    if (mlir::failed(pm.run(*mlir_module))) {
        std::fprintf(stderr, "logosc: MLIR lowering failed\n");
        return 1;
    }

    // ── MLIR LLVM dialect → LLVM IR ────────────────────────────
    mlir::registerBuiltinDialectTranslation(mlir_ctx);
    mlir::registerLLVMDialectTranslation(mlir_ctx);
    llvm::LLVMContext llvm_ctx;
    auto llvm_module = mlir::translateModuleToLLVMIR(*mlir_module, llvm_ctx);
    if (!llvm_module) {
        std::fprintf(stderr, "logosc: LLVM IR translation failed\n");
        return 1;
    }

    // --dump-metaprog phase 3: post-mlirgen LLVM IR snapshot.
    if (!opts.dump_metaprog_dir.empty()) {
        std::string ll_text;
        {
            llvm::raw_string_ostream os(ll_text);
            llvm_module->print(os, nullptr);
        }
        std::string path = opts.dump_metaprog_dir + "/_global_post_mlirgen.ll";
        if (FILE* f = std::fopen(path.c_str(), "w")) {
            std::fwrite(ll_text.data(), 1, ll_text.size(), f);
            std::fclose(f);
        }
    }

    llvm_module->setTargetTriple(llvm::Triple(llvm::sys::getDefaultTargetTriple()));

    if (opts.emit_llvm) { llvm_module->print(llvm::outs(), nullptr); return 0; }
    if (opts.jit_module_out) { *opts.jit_module_out = std::move(llvm_module); return 0; }

    // ── LLVM IR → object file ──────────────────────────────────
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    std::string err;
    auto* target = llvm::TargetRegistry::lookupTarget(
        llvm_module->getTargetTriple(), err);
    if (!target) {
        std::fprintf(stderr, "logosc: target lookup failed: %s\n", err.c_str());
        return 1;
    }

    auto llvm_opt = [&]() -> llvm::CodeGenOptLevel {
        switch (opts.opt_level) {
            case 1: return llvm::CodeGenOptLevel::Less;
            case 2: return llvm::CodeGenOptLevel::Default;
            case 3: return llvm::CodeGenOptLevel::Aggressive;
            default: return llvm::CodeGenOptLevel::None;
        }
    }();
    auto pb_opt = [&]() -> llvm::OptimizationLevel {
        switch (opts.opt_level) {
            case 1: return llvm::OptimizationLevel::O1;
            case 2: return llvm::OptimizationLevel::O2;
            case 3: return llvm::OptimizationLevel::O3;
            default: return llvm::OptimizationLevel::O0;
        }
    }();

    llvm::TargetOptions tmopts;
    tmopts.FunctionSections = opts.function_sections;
    tmopts.DataSections     = opts.function_sections;
    auto target_machine = std::unique_ptr<llvm::TargetMachine>(
        target->createTargetMachine(
            llvm_module->getTargetTriple(), "generic", "",
            tmopts, llvm::Reloc::PIC_,
            std::nullopt, llvm_opt));

    llvm_module->setDataLayout(target_machine->createDataLayout());

    // O1+: run LLVM optimization pipeline.
    if (opts.opt_level > 0) {
        llvm::LoopAnalysisManager     lam;
        llvm::FunctionAnalysisManager fam;
        llvm::CGSCCAnalysisManager    cgam;
        llvm::ModuleAnalysisManager   mam;
        llvm::PassBuilder pb(target_machine.get());
        pb.registerModuleAnalyses(mam);
        pb.registerCGSCCAnalyses(cgam);
        pb.registerFunctionAnalyses(fam);
        pb.registerLoopAnalyses(lam);
        pb.crossRegisterProxies(lam, fam, cgam, mam);
        auto mpm = pb.buildPerModuleDefaultPipeline(pb_opt);
        mpm.run(*llvm_module, mam);
    }

    std::error_code ec;
    llvm::raw_fd_ostream out(output_path, ec, llvm::sys::fs::OF_None);
    if (ec) {
        std::fprintf(stderr, "logosc: cannot open output '%s': %s\n",
                     output_path.c_str(), ec.message().c_str());
        return 1;
    }
    llvm::legacy::PassManager pass;
    if (target_machine->addPassesToEmitFile(pass, out, nullptr,
                                             llvm::CodeGenFileType::ObjectFile)) {
        std::fprintf(stderr, "logosc: target cannot emit object file\n");
        return 1;
    }
    pass.run(*llvm_module);
    out.flush();
    return 0;
}

} // namespace logos::compiler
