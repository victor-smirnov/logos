// Logos project — https://github.com/victor-smirnov/logos
//
// Phase 4 / Slice 1 — JIT smoke test.
//
// Builds a hand-written LLVM IR module containing
//
//     i32 @add(i32 %a, i32 %b) { %s = add i32 %a, %b; ret i32 %s }
//
// JITs it, looks up the symbol, calls it, asserts result == 42.

#include <logos/jit/jit.hpp>

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>

#include <cstdio>
#include <cstdlib>

namespace {

std::pair<std::unique_ptr<llvm::Module>, std::unique_ptr<llvm::LLVMContext>>
build_add_module() {
    auto ctx = std::make_unique<llvm::LLVMContext>();
    auto mod = std::make_unique<llvm::Module>("smoke", *ctx);
    llvm::IRBuilder<> b(*ctx);
    auto* i32 = llvm::Type::getInt32Ty(*ctx);
    auto* fty = llvm::FunctionType::get(i32, {i32, i32}, false);
    auto* fn  = llvm::Function::Create(
        fty, llvm::Function::ExternalLinkage, "add", mod.get());
    auto args = fn->arg_begin();
    auto* a = args++; auto* z = args;
    auto* bb = llvm::BasicBlock::Create(*ctx, "entry", fn);
    b.SetInsertPoint(bb);
    b.CreateRet(b.CreateAdd(a, z));
    if (llvm::verifyFunction(*fn, &llvm::errs())) std::abort();
    return {std::move(mod), std::move(ctx)};
}

} // namespace

int main() {
    logos::jit::Jit jit;
    if (!jit.init()) {
        std::fprintf(stderr, "init: %s\n", jit.error_str().c_str());
        return 1;
    }

    auto [mod, ctx] = build_add_module();
    if (!jit.add_module(std::move(mod), std::move(ctx))) {
        std::fprintf(stderr, "add_module: %s\n", jit.error_str().c_str());
        return 1;
    }

    auto* sym = jit.lookup("add");
    if (!sym) {
        std::fprintf(stderr, "lookup: %s\n", jit.error_str().c_str());
        return 1;
    }
    auto* add = reinterpret_cast<int (*)(int, int)>(sym);

    int r = add(20, 22);
    if (r != 42) {
        std::fprintf(stderr, "add(20,22) = %d, expected 42\n", r);
        return 1;
    }

    std::printf("jit smoke: add(20,22) = %d  OK\n", r);
    return 0;
}
