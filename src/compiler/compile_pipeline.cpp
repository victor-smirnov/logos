// Shared MLIR/LLVM lowering tail. See compile_pipeline.hpp for the contract.
//
// Body extracted verbatim from the duplicated chunks at the end of
// main.cpp's user pipeline and emit_module.cpp's compile_to_object.
#include "compile_pipeline.hpp"
#include "mlir_gen.hpp"
#include "llvm_compat.hpp"

#include <mlir/IR/MLIRContext.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/Dialect/ControlFlow/IR/ControlFlowOps.h>
#include <mlir/Dialect/LLVMIR/LLVMDialect.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Pass/PassManager.h>
#include <mlir/Transforms/RegionUtils.h>
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
#include <llvm/TargetParser/Host.h>
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

    auto mlir_module = mlir_gen(mlir_ctx, prog, opts.debug_info, opts.source_path,
                                opts.overflow_checks);
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
    // Erase unreachable blocks before lowering. A match with an irrefutable
    // early arm (e.g. `0 | _ => …` ahead of a `_ => …`) leaves the later arm's
    // block with no predecessors; its `arith.constant`s never get converted by
    // the arith→LLVM pass and then fail `translateModuleToLLVMIR` ("missing
    // LLVMTranslationDialectInterface registration for arith.constant"). This is
    // a surgical dead-block sweep (no folding of live code), unlike a full
    // canonicalize.
    {
        mlir::IRRewriter rewriter(&mlir_ctx);
        mlir_module->walk([&](mlir::func::FuncOp fn) {
            if (!fn.getBody().empty())
                (void)mlir::eraseUnreachableBlocks(rewriter, fn.getBody());
        });
    }
    mlir::PassManager pm(&mlir_ctx);
    pm.addPass(logos::compat::create_scf_to_cf_pass());
    pm.addPass(mlir::createConvertControlFlowToLLVMPass());
    pm.addPass(mlir::createArithToLLVMConversionPass());
    pm.addPass(mlir::createConvertFuncToLLVMPass());
    pm.addPass(mlir::createReconcileUnrealizedCastsPass());
    if (mlir::failed(pm.run(*mlir_module))) {
        std::fprintf(stderr, "logosc: MLIR lowering failed\n");
        return 1;
    }

    // ── Materialise static vtable globals (Rust-style .rodata vtables) ──
    // mlir-gen emitted, per (trait,type), a zero-init `constant [N x ptr]`
    // placeholder vtable global + recorded its ordered method symbols in the
    // `logos.vtable_specs` module attr. NOW (post func→llvm) the methods are
    // `llvm.func`, so `llvm.mlir.addressof` of them is valid — rebuild each
    // placeholder's initializer as a static array of method addresses. The
    // result is a read-only relocated constant; no heap, no runtime init
    // (vs. the former malloc-per-coercion vtable leak).
    {
        mlir::ModuleOp mod = *mlir_module;
        if (auto specs = mod->getAttrOfType<mlir::ArrayAttr>("logos.vtable_specs")) {
            mlir::OpBuilder b(&mlir_ctx);
            auto ptr_t = mlir::LLVM::LLVMPointerType::get(&mlir_ctx);
            for (auto specAttr : specs) {
                auto one = mlir::cast<mlir::ArrayAttr>(specAttr);
                if (one.empty()) continue;
                auto sym = mlir::cast<mlir::StringAttr>(one[0]).getValue();
                auto glob = mod.lookupSymbol<mlir::LLVM::GlobalOp>(sym);
                if (!glob) continue;  // coercion site DCE'd → global unused
                size_t n = one.size() - 1;
                auto arr_type = mlir::LLVM::LLVMArrayType::get(ptr_t, n ? n : 1);
                auto& ir = glob.getInitializerRegion();
                ir.getBlocks().clear();
                auto* blk = b.createBlock(&ir);
                b.setInsertionPointToStart(blk);
                auto loc = glob.getLoc();
                mlir::Value arr = b.create<mlir::LLVM::ZeroOp>(loc, arr_type);
                for (size_t i = 0; i < n; ++i) {
                    auto m = mlir::cast<mlir::StringAttr>(one[i + 1]).getValue();
                    if (m.empty()) continue;  // object-unsafe sentinel → null slot
                    // Rust-faithful vtable header carries usize size/align as
                    // ptr-sized slots — encoded by build_inline_vtable as
                    // `__logos_lit__<N>` strings; here we emit IntToPtr of the
                    // constant instead of AddressOf, keeping the table homogeneous.
                    static constexpr llvm::StringRef LIT_PFX = "__logos_lit__";
                    if (m.starts_with(LIT_PFX)) {
                        int64_t lit = 0;
                        m.substr(LIT_PFX.size()).getAsInteger(10, lit);
                        auto c = b.create<mlir::LLVM::ConstantOp>(
                            loc, b.getI64IntegerAttr(lit));
                        mlir::Value pv = b.create<mlir::LLVM::IntToPtrOp>(loc, ptr_t, c.getResult());
                        arr = b.create<mlir::LLVM::InsertValueOp>(
                            loc, arr, pv, llvm::ArrayRef<int64_t>{(int64_t)i});
                        continue;
                    }
                    // Stored super-vtable pointer (Rust trait-upcasting): the
                    // slot is the address of ANOTHER vtable GLOBAL (not a func),
                    // emitted by ensure_vtable_global for each supertrait so an
                    // `&dyn Sub → &dyn Super` upcast can recover Super's vtable.
                    static constexpr llvm::StringRef VTREF_PFX = "__logos_vtref__";
                    if (m.starts_with(VTREF_PFX)) {
                        auto gsym = m.substr(VTREF_PFX.size());
                        if (!mod.lookupSymbol<mlir::LLVM::GlobalOp>(gsym)) continue;
                        mlir::Value ga = b.create<mlir::LLVM::AddressOfOp>(loc, ptr_t, gsym);
                        arr = b.create<mlir::LLVM::InsertValueOp>(
                            loc, arr, ga, llvm::ArrayRef<int64_t>{(int64_t)i});
                        continue;
                    }
                    if (!mod.lookupSymbol<mlir::LLVM::LLVMFuncOp>(m)) continue;
                    mlir::Value fa = b.create<mlir::LLVM::AddressOfOp>(loc, ptr_t, m);
                    arr = b.create<mlir::LLVM::InsertValueOp>(
                        loc, arr, fa, llvm::ArrayRef<int64_t>{(int64_t)i});
                }
                b.create<mlir::LLVM::ReturnOp>(loc, arr);
            }
            mod->removeAttr("logos.vtable_specs");
        }
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

    logos::compat::set_default_target_triple(*llvm_module);

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
    // Target CPU: "generic" (default — portable x86-64 baseline, SSE2) unless the
    // caller asked for a specific cpu via `-C target-cpu=`. "native" resolves to
    // the host CPU, letting the backend emit AVX/AVX2/AVX-512 (a large win on
    // vectorizable loops; non-portable). The CPU name alone enables that CPU's
    // default feature set in the backend.
    std::string cpu = opts.target_cpu.empty() ? "generic" : opts.target_cpu;
    if (cpu == "native") cpu = std::string(llvm::sys::getHostCPUName());
    auto target_machine = std::unique_ptr<llvm::TargetMachine>(
        target->createTargetMachine(
            llvm_module->getTargetTriple(), cpu, "",
            tmopts, llvm::Reloc::PIC_,
            std::nullopt, llvm_opt));

    llvm_module->setDataLayout(target_machine->createDataLayout());

    // O1+: run LLVM optimization pipeline.
    if (opts.opt_level > 0) {
        llvm::LoopAnalysisManager     lam;
        llvm::FunctionAnalysisManager fam;
        llvm::CGSCCAnalysisManager    cgam;
        llvm::ModuleAnalysisManager   mam;
        // PipelineTuningOptions defaults SLPVectorization to FALSE — so the
        // default-constructed PassBuilder NEVER ran the SLP (straight-line)
        // vectorizer, and logosc emitted scalar code where rustc/clang pack
        // adjacent independent ops (e.g. struct x/y/z float triples, unrolled
        // loop bodies) into SIMD. clang/rustc enable both vectorizers at O2+;
        // match that. (LoopVectorization defaults true, but set it explicitly.)
        llvm::PipelineTuningOptions pto;
        pto.LoopVectorization = opts.opt_level > 1;
        pto.SLPVectorization  = opts.opt_level > 1;
        llvm::PassBuilder pb(target_machine.get(), pto);
        pb.registerModuleAnalyses(mam);
        pb.registerCGSCCAnalyses(cgam);
        pb.registerFunctionAnalyses(fam);
        pb.registerLoopAnalyses(lam);
        pb.crossRegisterProxies(lam, fam, cgam, mam);
        auto mpm = pb.buildPerModuleDefaultPipeline(pb_opt);
        mpm.run(*llvm_module, mam);
    }

    // Post-optimization IR dump (counterpart to the PRE-opt --emit-llvm above).
    // Honors opt_level: with -O0 nothing ran, so this prints the same module as
    // --emit-llvm; with -O2/-O3 it shows the inlined/optimized result.
    if (opts.emit_llvm_opt) { llvm_module->print(llvm::outs(), nullptr); return 0; }

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
