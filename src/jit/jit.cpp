// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/jit/jit.hpp>

#include <llvm/ExecutionEngine/Orc/AbsoluteSymbols.h>
#include <llvm/ExecutionEngine/Orc/CoreContainers.h>
#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/TargetSelect.h>

namespace logos::jit {

struct Jit::Impl {
    std::unique_ptr<llvm::orc::LLJIT> lljit;
};

Jit::Jit() : impl_(std::make_unique<Impl>()) {}
Jit::~Jit() = default;
Jit::Jit(Jit&&) noexcept = default;
Jit& Jit::operator=(Jit&&) noexcept = default;

static std::string take_err(llvm::Error e) {
    std::string s;
    llvm::handleAllErrors(std::move(e), [&](const llvm::ErrorInfoBase& eib) {
        if (!s.empty()) s += "; ";
        s += eib.message();
    });
    return s;
}

bool Jit::init() {
    // Idempotent target init — safe to call multiple times.
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    auto jit = llvm::orc::LLJITBuilder().create();
    if (!jit) {
        last_err_ = take_err(jit.takeError());
        return false;
    }
    impl_->lljit = std::move(*jit);
    return true;
}

bool Jit::add_module(std::unique_ptr<llvm::Module>      mod,
                     std::unique_ptr<llvm::LLVMContext> ctx) {
    if (!impl_->lljit) { last_err_ = "Jit::init not called"; return false; }
    llvm::orc::ThreadSafeModule tsm(std::move(mod), std::move(ctx));
    if (auto err = impl_->lljit->addIRModule(std::move(tsm))) {
        last_err_ = take_err(std::move(err));
        return false;
    }
    return true;
}

bool Jit::define_symbol(std::string_view name, void* addr) {
    if (!impl_->lljit) { last_err_ = "Jit::init not called"; return false; }
    auto& jd = impl_->lljit->getMainJITDylib();
    llvm::orc::SymbolMap syms;
    syms[impl_->lljit->mangleAndIntern(llvm::StringRef(name.data(), name.size()))] = {
        llvm::orc::ExecutorAddr::fromPtr(addr),
        llvm::JITSymbolFlags::Exported | llvm::JITSymbolFlags::Callable,
    };
    if (auto err = jd.define(llvm::orc::absoluteSymbols(std::move(syms)))) {
        last_err_ = take_err(std::move(err));
        return false;
    }
    return true;
}

bool Jit::enable_process_symbols() {
    if (!impl_->lljit) { last_err_ = "Jit::init not called"; return false; }
    auto& jd = impl_->lljit->getMainJITDylib();
    auto gen = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
        impl_->lljit->getDataLayout().getGlobalPrefix());
    if (!gen) {
        last_err_ = take_err(gen.takeError());
        return false;
    }
    jd.addGenerator(std::move(*gen));
    return true;
}

void* Jit::lookup(std::string_view name) {
    if (!impl_->lljit) { last_err_ = "Jit::init not called"; return nullptr; }
    auto sym = impl_->lljit->lookup(llvm::StringRef(name.data(), name.size()));
    if (!sym) {
        last_err_ = take_err(sym.takeError());
        return nullptr;
    }
    return reinterpret_cast<void*>(sym->getValue());
}

} // namespace logos::jit
