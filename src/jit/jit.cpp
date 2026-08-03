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
#include <llvm/ExecutionEngine/ObjectCache.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SHA256.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace logos::jit {

// ── Persistent object cache ─────────────────────────────────────────────────
//
// The key is a SHA-256 over (module bitcode || salt). Hashing the bitcode costs
// a serialisation of the module on every build, hit or miss — paid deliberately,
// because the alternative keys are all unsound: the module's identifier is not
// unique, a source mtime says nothing about a module synthesised in memory, and
// a "did anything change" heuristic is exactly how a cache serves stale code.
// The one thing this must never do is return an object for a module it was not
// compiled from.
namespace {
class LogosObjectCache final : public llvm::ObjectCache {
public:
    LogosObjectCache(std::string dir, std::string salt)
        : dir_(std::move(dir)), salt_(std::move(salt)) {}

    void notifyObjectCompiled(const llvm::Module* M, llvm::MemoryBufferRef Obj) override {
        if (!M) return;
        // ⚠ THE KEY IS CARRIED FROM getObject, NOT RECOMPUTED HERE.
        // MEASURED: hashing the module at this point yields a DIFFERENT key than
        // the lookup did moments earlier — 4 595 272 bytes of bitcode on the way
        // in, 4 507 340 on the way out. The codegen pipeline MUTATES the module
        // it is handed, so a key derived from the post-compile module names an
        // object nobody will ever ask for: every build stored a 6 MB file and
        // every build missed. The cache "worked" — it wrote, the directory
        // filled, the run took exactly as long as before.
        //
        // This is the same defect class the compiler itself keeps meeting: a
        // fact re-derived from a representation that has since changed, instead
        // of carried from where it was known.
        std::string path;
        {
            std::lock_guard<std::mutex> g(mu_);
            if (auto it = pending_.find(M); it != pending_.end()) {
                path = it->second;
                pending_.erase(it);
            }
        }
        if (path.empty()) {
            // No pending key means getObject was never called for this module —
            // the compile did not go through the path this cache observes. Do
            // NOT invent a key from the mutated module: an entry under a key
            // that can never be recomputed is indistinguishable from a leak.
            ++orphans_;
            return;
        }
        // Write to a unique temp then rename: two builds may race on the same
        // key, and a half-written object read as complete is a crash with no
        // explanation. rename(2) within a directory is atomic.
        std::error_code ec;
        llvm::SmallString<256> tmp(path);
        tmp += ".tmp.";
        tmp += std::to_string(::getpid());
        {
            llvm::raw_fd_ostream out(tmp, ec, llvm::sys::fs::OF_None);
            if (ec) return;
            out << Obj.getBuffer();
            out.flush();
            if (out.has_error()) { out.clear_error(); llvm::sys::fs::remove(tmp); return; }
        }
        if (llvm::sys::fs::rename(tmp, path)) llvm::sys::fs::remove(tmp);
        else ++stores_;
    }

    std::unique_ptr<llvm::MemoryBuffer> getObject(const llvm::Module* M) override {
        if (!M) return nullptr;
        // The key is computed HERE, from the module as the compiler received it,
        // and remembered for the notify that follows.
        auto path = path_for(*M);
        if (path.empty()) return nullptr;
        {
            std::lock_guard<std::mutex> g(mu_);
            pending_[M] = path;
        }
        auto buf = llvm::MemoryBuffer::getFile(path, /*IsText=*/false);
        if (!buf) { ++misses_; return nullptr; }
        ++hits_;
        return std::move(*buf);
    }

    long hits() const { return hits_; }
    long orphans() const { return orphans_; }
    long misses() const { return misses_; }
    long stores() const { return stores_; }

private:
    std::string path_for(const llvm::Module& M) const {
        std::string bc;
        {
            llvm::raw_string_ostream os(bc);
            llvm::WriteBitcodeToFile(M, os);
        }
        llvm::SHA256 h;
        h.update(llvm::StringRef(salt_));
        h.update(llvm::StringRef(bc));
        auto digest = h.final();
        std::string hex;
        hex.reserve(64);
        static const char* kHex = "0123456789abcdef";
        for (uint8_t b : digest) { hex.push_back(kHex[b >> 4]); hex.push_back(kHex[b & 15]); }
        // The key is printed on demand because "the cache did not help" has two
        // very different causes — the object is never consulted, or the key
        // differs between runs — and only the key itself tells them apart.
        if (const char* t = std::getenv("LOGOS_TRACE_JIT_CACHE"); t && t[0] && t[0] != '0')
            std::fprintf(stderr, "jit-cache: key=%s bitcode=%zu bytes module='%s'\n",
                         hex.c_str(), bc.size(), M.getModuleIdentifier().c_str());
        return dir_ + "/" + hex + ".o";
    }
    std::string dir_, salt_;
    mutable std::mutex mu_;
    std::unordered_map<const llvm::Module*, std::string> pending_;
    mutable std::atomic<long> hits_{0}, misses_{0}, stores_{0}, orphans_{0};
};
}  // namespace

struct Jit::Impl {
    std::unique_ptr<llvm::orc::LLJIT> lljit;
    std::unique_ptr<LogosObjectCache> cache;
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
        // The compiler is ALWAYS replaced, not only when a cache is present.
        // It carried two settings and one of them was gated on the cache by
        // accident: with `LOGOS_JIT_CACHE=0` the codegen level silently
        // reverted to the default, which is the -O2-equivalent this exists to
        // avoid. A knob that turns off an unrelated thing is a trap, and it
        // fooled the measurement that found it.
        auto* cache = impl_->cache.get();   // may be null — the cache is optional
        builder.setCompileFunctionCreator(
            [cache](llvm::orc::JITTargetMachineBuilder JTMB)
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
                    std::move(*tm), cache);
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

bool Jit::enable_object_cache(std::string_view cache_dir, std::string_view key_salt) {
    if (cache_dir.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(std::string(cache_dir)), ec);
    if (ec) {
        last_err_ = "object cache: cannot create '" + std::string(cache_dir) + "': " + ec.message();
        return false;
    }
    impl_->cache = std::make_unique<LogosObjectCache>(std::string(cache_dir),
                                                      std::string(key_salt));
    return true;
}

Jit::CacheStats Jit::cache_stats() const noexcept {
    if (!impl_->cache) return {};
    return {impl_->cache->hits(), impl_->cache->misses(), impl_->cache->stores()};
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
