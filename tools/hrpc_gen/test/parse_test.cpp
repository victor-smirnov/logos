// Logos project — https://github.com/victor-smirnov/logos

// Smoke test: parse an .hrpc file and dump the AST.

#include "hrpc_idl_parser.hpp"

#include <logos/hermes/document.hpp>
#include <logos/hermes/view.hpp>
#include <logos/hermes/compat.hpp>
#include <logos/hermes/stringify.hpp>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <cstdlib>

using namespace logos::hrpc;

static std::string read_file(const char* path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "cannot open: " << path << "\n";
        std::exit(1);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: hrpc_parse_test <file.hrpc>\n";
        return 1;
    }

    std::string source = read_file(argv[1]);
    HrpcIdlParser parser(source);
    auto doc = parser.parse_file();

    // Stringify the Hermes document to see the AST.
    std::string json = logos::hermes::stringify(doc);
    std::cout << json << "\n";

    // Basic sanity checks.
    auto root = doc.root_object().as_tiny_map();
    auto code = root.get(hrpc_idl_ast::CODE).as_value<int32_t>();
    if (code != hrpc_idl_ast::FILE.code) {
        std::cerr << "FAIL: root node is not FILE (got " << code << ")\n";
        return 1;
    }

    std::cout << "PASS: parsed successfully, root node = FILE\n";
    return 0;
}
