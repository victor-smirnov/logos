// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov

#pragma once
#include "module_manifest.hpp"
#include <string>
#include <vector>

namespace logos::compiler {

struct EmitModuleOptions {
    std::vector<std::string> extra_search_paths;  // -I flags
    bool emit_mlir = false;
    bool emit_llvm = false;
};

// Build a binary module (.a archive) from a module manifest.
// Returns true on success.
bool emit_module(const ModuleManifest& manifest,
                 const std::string& output_path,
                 const EmitModuleOptions& opts);

} // namespace logos::compiler
