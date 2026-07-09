
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
        "  --ast-header <f>   Also emit the root grammar's %fields/%nodes as a\n"
        "                     standalone C++ constants header at <f>\n"
        "  --check-schema <f> Cross-check every %schema block against the ADR-0011\n"
        "                     `schema` items in <f> (repeatable). Emits nothing.\n"
        "  --help             Show this message",
        argv0);
}

int main(int argc, char* argv[]) {
    std::string grammar_file;
    fs::path    out_dir = ".";
    fs::path    ast_header;
    std::vector<fs::path> check_schema;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if ((arg == "--out-dir") && i + 1 < argc)  out_dir = argv[++i];
        else if ((arg == "--ast-header") && i + 1 < argc) ast_header = argv[++i];
        else if ((arg == "--check-schema") && i + 1 < argc) check_schema.emplace_back(argv[++i]);
        else if (arg == "--help") { usage(argv[0]); return 0; }
        else if (!arg.starts_with("--")) grammar_file = std::string(arg);
        else { std::println(stderr, "Unknown option: {}", arg); return 1; }
    }

    if (grammar_file.empty()) { usage(argv[0]); return 1; }

    auto modules = resolve_modules(grammar_file);
    if (!modules) return 1;

    // Check-only mode: verify the mirror, emit nothing.
    if (!check_schema.empty())
        return check_schema_mirror(*modules, check_schema) ? 0 : 1;

    codegen(*modules, CodegenOptions{ .output_dir = out_dir,
                                      .ast_header = ast_header });
    return 0;
}
