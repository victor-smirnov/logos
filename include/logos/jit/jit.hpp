// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
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

    // Returns the JIT'd address of `name`, or nullptr on miss.
    void* lookup(std::string_view name);

    const std::string& error_str() const noexcept { return last_err_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string           last_err_;
};

} // namespace logos::jit
