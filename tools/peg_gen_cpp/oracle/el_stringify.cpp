// Logos project — https://github.com/victor-smirnov/logos
//
// EL cross-BACKEND oracle, C++ side.
//
// Parses each line of a corpus with the peg_gen_cpp-generated EL parser and
// prints the structural stringification of the resulting SExpr. The Logos side
// (el_sdump.logos) does the same through the peg_gen_logos-generated parser.
// Identical output ⇒ one grammar, two backends, one Writ.
//
// Note the entry shape: el.peg declares `arena: external`, so the parser
// BORROWS the document and hands back the root edge (`AnyVal`), rather than
// owning and returning a `Writ`.

#include "el_parser.hpp"

#include <logos/writ/compat.hpp>
#include <logos/writ/stringify.hpp>

#include <cstdio>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: el_stringify <corpus-file>\n");
        return 2;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::fprintf(stderr, "el_stringify: cannot open '%s'\n", argv[1]);
        return 2;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;   // blank / comment
        auto doc = logos::writ::make_doc(1u << 16).get();
        logos::wql::el::ElParser p(line, doc);
        logos::writ::AnyVal root = p.parse_expr();
        if (root.is_null()) {
            std::printf("<parse-error>\n");
            continue;
        }
        std::printf("%s\n", logos::writ::stringify_value(root).c_str());
    }
    return 0;
}
