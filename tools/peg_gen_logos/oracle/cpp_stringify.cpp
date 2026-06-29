// AST-equality oracle — C++ side.
//
// Parse a .logos file with the C++-generated logos_parser (the one peg_gen_cpp
// emits from logos.peg and embeds in logosc) and print the canonical text form of
// the resulting AST via the C++ writ::stringify. The Logos-side harness
// (oracle/sdump.logos, linking the peg_gen_logos-generated parser) prints the same
// canonical form; run.sh diffs the two over a corpus. Byte-identical output ⇒ the
// two generated parsers agree on the AST.
#include "logos_parser.hpp"
#include <logos/writ/stringify.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <file.logos>\n", argv[0]); return 2; }
    std::ifstream f(argv[1]);
    std::stringstream ss; ss << f.rdbuf();
    std::string src = ss.str();
    logos::compiler::LogosParser p(src);
    auto doc = p.parse_module();
    std::printf("%s\n", logos::writ::stringify(doc).c_str());
    return 0;
}
