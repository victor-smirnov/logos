// Logos project — https://github.com/victor-smirnov/logos
//
// jit.hpp — minimal LLVM ORC LLJIT wrapper.
//
// Slice 1 of Phase 4: standalone JIT engine. No Hermes integration, no
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
