// Logos project — https://github.com/victor-smirnov/logos

#include <logos/jit/jit.hpp>

#include <llvm/ExecutionEngine/Orc/AbsoluteSymbols.h>
#include <llvm/ExecutionEngine/Orc/CoreContainers.h>
#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ObjectFileInterface.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>

#include <cstdio>
#include <cstdlib>

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

    llvm::orc::LLJITBuilder builder;
    {
        // The compiler is replaced UNCONDITIONALLY. It used to be installed only
        // alongside an object cache, so disabling the cache silently restored
        // the default -O2-equivalent codegen level — and the measurement meant
        // to prove the level mattered used exactly that flag to "clean" the
        // conditions, saw no change, and was believed for a round.
        builder.setCompileFunctionCreator(
            [](llvm::orc::JITTargetMachineBuilder JTMB)
                -> llvm::Expected<std::unique_ptr<llvm::orc::IRCompileLayer::IRCompiler>> {
                // ⚠ THE METAPROGRAM RUNS ONCE, FOR 0 ms. MEASURED.
                // LLJIT defaults to the TargetMachine's -O2-equivalent codegen,
                // and this module's cost is concentrated in a few ENORMOUS
                // generated functions — one 71 KB table constructor, one 54 KB
                // renderer, one 21 KB params builder are together half of all
                // the code the closure contains. Optimising them is spending
                // minutes to make something faster that executes for zero
                // milliseconds and is then thrown away. Compile-time is the only
                // thing that matters for a compile-time-only module, so the
                // codegen level is set accordingly.
                if (const char* o = std::getenv("LOGOS_METAJIT_OPT");
                    o && o[0] == '2')
                    JTMB.setCodeGenOptLevel(llvm::CodeGenOptLevel::Default);
                else
                    JTMB.setCodeGenOptLevel(llvm::CodeGenOptLevel::None);
                auto tm = JTMB.createTargetMachine();
                if (!tm) return tm.takeError();
                return std::make_unique<llvm::orc::TMOwningSimpleCompiler>(
                    std::move(*tm));
            });
    }
    auto jit = builder.create();
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

bool Jit::add_static_archive(std::string_view path) {
    if (!impl_->lljit) { last_err_ = "Jit::init not called"; return false; }
    auto& jd = impl_->lljit->getMainJITDylib();
    std::string p(path);
    // Tier-agnostic metacall: the layer archives legitimately carry duplicate
    // copies of the same symbol — a lower layer (e.g. logos-lcm) codegens an
    // ast_only metaprog generic, and a higher layer (logos-std) that uses it via
    // a derive/mapping re-instantiates the SAME generic into its own object (the
    // ast_only body is absent from the lower layer's binary index, so mono can't
    // reference it and re-instantiates). Loading several of these archives into
    // one JITDylib then hits "Duplicate definition". The duplicates are benign
    // (identical code), so mark every static-archive symbol Weak: ORC keeps one
    // definition and drops the rest instead of erroring. This makes the metacall
    // robust to WHICH tier metaprog (or any shared generic) lives in.
    auto weak_iface = [](llvm::orc::ExecutionSession& es,
                         llvm::MemoryBufferRef buf)
        -> llvm::Expected<llvm::orc::MaterializationUnit::Interface> {
        auto iface = llvm::orc::getObjectFileInterface(es, buf);
        if (!iface) return iface.takeError();
        for (auto& kv : iface->SymbolFlags)
            kv.second |= llvm::JITSymbolFlags::Weak;
        return iface;
    };
    auto gen = llvm::orc::StaticLibraryDefinitionGenerator::Load(
        impl_->lljit->getObjLinkingLayer(), p.c_str(),
        llvm::orc::StaticLibraryDefinitionGenerator::VisitMembersFunction(),
        std::move(weak_iface));
    if (!gen) {
        last_err_ = take_err(gen.takeError());
        return false;
    }
    jd.addGenerator(std::move(*gen));
    return true;
}

bool Jit::enable_process_symbols() {
    if (!impl_->lljit) { last_err_ = "Jit::init not called"; return false; }
    auto& jd = impl_->lljit->getMainJITDylib();
    // The Logos metaprog JIT loads stdlib runtime objects that reference
    // io_uring (thread_uring.c). On distros that bundle a STATIC liburing.a it
    // is loaded into the JIT via add_static_archive; where only the shared
    // liburing.so exists (e.g. Fedora) there is no archive to load, so make
    // its symbols visible to the process-symbol generator below by loading the
    // shared library permanently. Best-effort: harmless/no-op where absent or
    // already resolved.
    llvm::sys::DynamicLibrary::LoadLibraryPermanently("liburing.so.2");
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
