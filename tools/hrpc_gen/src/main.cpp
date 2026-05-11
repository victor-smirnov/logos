// Logos project — https://github.com/victor-smirnov/logos
//
// hrpc_gen — HRPC IDL compiler.
//
// Usage:
//   hrpc_gen <file.hrpc> --out-dir <dir>
//
// Outputs:
//   <out-dir>/<basename>.gen.hpp
//   <out-dir>/<basename>.gen.cpp

#include "codegen.hpp"
#include "hrpc_idl_parser.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "hrpc_gen: cannot open: " << path << "\n";
        std::exit(1);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void usage() {
    std::cerr << "usage: hrpc_gen <file.hrpc> [--out-dir <dir>]\n";
    std::exit(1);
}

int main(int argc, char** argv) {
    if (argc < 2) usage();

    std::string input_path;
    std::string out_dir = ".";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--out-dir") {
            if (++i >= argc) usage();
            out_dir = argv[i];
        } else if (arg == "--help" || arg == "-h") {
            usage();
        } else if (!arg.empty() && arg[0] != '-') {
            input_path = arg;
        } else {
            std::cerr << "hrpc_gen: unknown option: " << arg << "\n";
            usage();
        }
    }

    if (input_path.empty()) usage();

    // Parse.
    std::string source = read_file(input_path);
    logos::hrpc::HrpcIdlParser parser(source);
    auto ast = parser.parse_file();
    if (ast.is_null()) {
        std::cerr << "hrpc_gen: parse error in " << input_path << "\n";
        return 1;
    }

    // Derive output file names from the input stem.
    namespace fs = std::filesystem;
    std::string stem   = fs::path(input_path).stem().string();
    std::string hdr    = stem + ".gen.hpp";
    std::string src    = stem + ".gen.cpp";
    std::string hdr_path = (fs::path(out_dir) / hdr).string();
    std::string src_path = (fs::path(out_dir) / src).string();

    // Generate.
    logos::hrpc_gen::CodeGen gen(std::move(ast), input_path);

    {
        std::ofstream out(hdr_path);
        if (!out) { std::cerr << "hrpc_gen: cannot write: " << hdr_path << "\n"; return 1; }
        // Header guard from file name, e.g. "ECHO_HRPC_GEN_HPP".
        std::string guard = stem + "_gen_hpp";
        for (char& c : guard) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        gen.emit_header(out, guard);
    }

    {
        std::ofstream out(src_path);
        if (!out) { std::cerr << "hrpc_gen: cannot write: " << src_path << "\n"; return 1; }
        gen.emit_source(out, hdr);
    }

    std::cerr << "hrpc_gen: wrote " << hdr_path << "\n";
    std::cerr << "hrpc_gen: wrote " << src_path << "\n";
    return 0;
}
