// Logos project — https://github.com/victor-smirnov/logos
//
// WQL cross-BACKEND oracle, C++ side.
//
// Parses each line of a corpus with the peg_gen_cpp-generated WQL surface parser
// and prints the structural stringification of the resulting RQProgram. The Logos
// side (wql_sdump.logos) does the same through the peg_gen_logos-generated parser.
//
// The sibling EL oracle covers only EXPRESSIONS. That gap is not hypothetical:
// `INTEGER` type suffixes diverged between the backends for four grammars because
// no corpus line ever wrote `1000000i64`. deem! bodies write them constantly, and
// nothing compared the two WQL parsers until this file existed.

#include <cstdio>
#include <fstream>
#include <string>

#include <logos/writ/document.hpp>
#include <logos/writ/stringify.hpp>

#include "wql_surface_parser.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: wql_stringify <corpus>\n");
        return 2;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::fprintf(stderr, "wql_stringify: cannot open '%s'\n", argv[1]);
        return 2;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;   // blank / comment
        auto doc = logos::writ::make_doc(1u << 18).get();
        logos::wql::surface::WqlParser p(line, doc);
        logos::writ::AnyVal root = p.parse_program();
        if (root.is_null()) {
            std::printf("<parse-error>\n");
            continue;
        }
        std::printf("%s\n", logos::writ::stringify_value(root).c_str());
    }
    return 0;
}
