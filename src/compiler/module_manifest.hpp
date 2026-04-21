// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — module manifest parser for `logosc --emit-module`.

#pragma once
#include <string>
#include <vector>
#include <optional>

namespace logos::compiler {

// Parsed logos.module manifest.
struct ModuleManifest {
    std::string name;      // e.g. "stdlogos"
    std::string version;   // e.g. "0.1"
    std::string root;      // directory containing .logos files (relative or absolute)
    std::vector<std::string> depends;  // other module names (for future use)
};

// Parse a logos.module manifest file.  Returns nullopt + message on error.
std::optional<ModuleManifest> parse_module_manifest(const std::string& path,
                                                    std::string& err_out);

} // namespace logos::compiler
