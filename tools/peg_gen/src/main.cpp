// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <print>
#include <string>
#include <string_view>
#include <filesystem>

#include "grammar_parser.hpp"
#include "module_resolver.hpp"
#include "codegen.hpp"

namespace fs = std::filesystem;
using namespace logos::peg_gen;

static void usage(std::string_view argv0) {
    std::println(stderr,
        "Usage: {} <grammar.peg> [options]\n"
        "\n"
        "Options:\n"
        "  --out-dir  <dir>   Output directory for generated files (default: .)\n"
        "  --tpl-dir  <dir>   Directory with .htpl templates (default: auto-detect)\n"
        "  --help             Show this message",
        argv0);
}

int main(int argc, char* argv[]) {
    std::string grammar_file;
    fs::path    out_dir   = ".";
    fs::path    tpl_dir;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if ((arg == "--out-dir") && i + 1 < argc)  out_dir  = argv[++i];
        else if ((arg == "--tpl-dir") && i + 1 < argc) tpl_dir = argv[++i];
        else if (arg == "--help") { usage(argv[0]); return 0; }
        else if (!arg.starts_with("--")) grammar_file = std::string(arg);
        else { std::println(stderr, "Unknown option: {}", arg); return 1; }
    }

    if (grammar_file.empty()) { usage(argv[0]); return 1; }

    // Auto-detect template directory relative to the executable.
    if (tpl_dir.empty()) {
        auto exe = fs::weakly_canonical(fs::path(argv[0])).parent_path();
        tpl_dir = exe / "../templates";  // works from build dir or install
        if (!fs::exists(tpl_dir))
            tpl_dir = fs::path(argv[0]).parent_path() / "templates";
    }

    if (!fs::exists(tpl_dir)) {
        std::println(stderr, "peg_gen: template directory not found: {}", tpl_dir.string());
        return 1;
    }

    // Resolve all imported grammars (topological order, root last).
    auto modules = resolve_modules(grammar_file);
    if (!modules) return 1;  // error already printed

    std::println("peg_gen: resolved {} grammar module(s)", modules->size());

    // Generate C++ for each module.
    CodegenOptions opts{
        .templates_dir = tpl_dir,
        .output_dir    = out_dir,
    };
    codegen(*modules, opts);

    return 0;
}
