// Logos project — https://github.com/victor-smirnov/logos
//
// jit.hpp — minimal LLVM ORC LLJIT wrapper.
//
// Slice 1 of Phase 4: standalone JIT engine. No Writ integration, no
// Logos-pipeline integration, no split-stack — just compile-and-run for
// hand-rolled LLVM IR modules. Used by the JIT smoke exerciser.

#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace llvm {
class Module;
class LLVMContext;
} // namespace llvm

namespace logos::jit {

class Jit {
public:
    Jit();
    ~Jit();
    Jit(Jit&&) noexcept;
    Jit& operator=(Jit&&) noexcept;
    Jit(const Jit&)            = delete;
    Jit& operator=(const Jit&) = delete;

    // Persistent object cache for JIT-compiled modules.
    //
    // MEASURED 2026-08-02: building the stdlib `mem` layer spends 243 s — 68% of
    // the whole layer — inside ONE `lookup()`. ORC compiles lazily but at MODULE
    // granularity, so the first lookup of any symbol compiles all 5 758 defined
    // functions / 49 793 basic blocks, single-threaded, and the machine code is
    // thrown away when the process exits. The very same module is recompiled on
    // the next build even when nothing about it changed — and today ANY logosc
    // edit rebuilds every stdlib layer, so that is the common case, not the rare
    // one.
    //
    // `cache_dir` is where compiled objects live. `key_salt` MUST carry every
    // input that changes generated code but is not in the module itself — LLVM
    // version, target triple/CPU, optimisation level, and the compiler's own
    // version. The cache key is a strong hash of the module's bitcode PLUS this
    // salt. Getting the key wrong does not produce a slow build, it produces
    // silently stale machine code, which is the one failure mode worth more care
    // than the speed-up is worth.
    //
    // Call BEFORE init(); returns false (and does not enable the cache) if the
    // directory cannot be created.
    bool enable_object_cache(std::string_view cache_dir, std::string_view key_salt);

    // Cache statistics for --stats: how many modules were served from disk and
    // how many had to be compiled. A cache nobody can measure is a cache nobody
    // can tell from a no-op.
    struct CacheStats { long hits = 0; long misses = 0; long stores = 0; };
    CacheStats cache_stats() const noexcept;

    // Build the underlying LLJIT. Must be called once before use.
    // Returns true on success; on failure, error_str() carries details.
    bool init();

    // Take ownership of a module + its context and hand it to LLJIT.
    bool add_module(std::unique_ptr<llvm::Module>      mod,
                    std::unique_ptr<llvm::LLVMContext> ctx);

    // Register a host pointer under `name` so JIT'd code can call it via
    // an `extern` declaration. Must be called before any module that
    // references the name is added (ORC resolves on add).
    bool define_symbol(std::string_view name, void* addr);

    // Add the host process's exported symbols (libc, libm, anything in
    // RTLD_DEFAULT) to the main JITDylib's lookup chain. After this,
    // JIT'd `extern` declarations for malloc/free/printf/memcpy/etc.
    // resolve automatically. Opt-in: not called by default — callers
    // that JIT untrusted code should leave it off.
    bool enable_process_symbols();

    // M.1 Stage 2 (Mode B): register a static archive (`.a`) so undefined
    // symbols in JIT'd modules resolve from the archive's compiled object
    // members. Used by metacall to invoke callees whose body lives only in
    // a linker-pulled binary. Path must be absolute or resolvable relative
    // to cwd; ORC mmaps the archive lazily.
    bool add_static_archive(std::string_view path);

    // Returns the JIT'd address of `name`, or nullptr on miss.
    void* lookup(std::string_view name);

    const std::string& error_str() const noexcept { return last_err_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string           last_err_;
};

} // namespace logos::jit
