// Logos project — https://github.com/victor-smirnov/logos
//
// Phase 4 / Slice 2 — host-symbol resolution.
//
// JIT-compiles a module that declares `extern i32 host_double(i32)` and
// calls it: `i32 @callit(i32 %x) { ret i32 host_double(%x) }`.
// We register `host_double` via Jit::define_symbol before adding the
// module; ORC resolves the external on link.

#include <logos/jit/jit.hpp>

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>

#include <cstdio>
#include <cstdlib>

extern "C" int host_double(int x) { return x * 2; }

namespace {

std::pair<std::unique_ptr<llvm::Module>, std::unique_ptr<llvm::LLVMContext>>
build_callit_module() {
    auto ctx = std::make_unique<llvm::LLVMContext>();
    auto mod = std::make_unique<llvm::Module>("host_call", *ctx);
    auto* i32 = llvm::Type::getInt32Ty(*ctx);

    auto* host_ty = llvm::FunctionType::get(i32, {i32}, false);
    auto* host    = llvm::Function::Create(
        host_ty, llvm::Function::ExternalLinkage, "host_double", mod.get());

    auto* fty = llvm::FunctionType::get(i32, {i32}, false);
    auto* fn  = llvm::Function::Create(
        fty, llvm::Function::ExternalLinkage, "callit", mod.get());

    llvm::IRBuilder<> b(*ctx);
    auto* bb = llvm::BasicBlock::Create(*ctx, "entry", fn);
    b.SetInsertPoint(bb);
    auto* x   = fn->arg_begin();
    auto* dbl = b.CreateCall(host, {x});
    b.CreateRet(dbl);

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

    if (!jit.define_symbol("host_double", reinterpret_cast<void*>(&host_double))) {
        std::fprintf(stderr, "define_symbol: %s\n", jit.error_str().c_str());
        return 1;
    }

    auto [mod, ctx] = build_callit_module();
    if (!jit.add_module(std::move(mod), std::move(ctx))) {
        std::fprintf(stderr, "add_module: %s\n", jit.error_str().c_str());
        return 1;
    }

    auto* sym = jit.lookup("callit");
    if (!sym) {
        std::fprintf(stderr, "lookup: %s\n", jit.error_str().c_str());
        return 1;
    }
    auto* callit = reinterpret_cast<int (*)(int)>(sym);

    int r = callit(21);
    if (r != 42) {
        std::fprintf(stderr, "callit(21) = %d, expected 42\n", r);
        return 1;
    }
    std::printf("jit host-call: callit(21) = %d  OK\n", r);
    return 0;
}
