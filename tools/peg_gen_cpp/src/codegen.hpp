// Logos project — https://github.com/victor-smirnov/logos
//
// codegen: generates C++ parser from resolved grammar modules.
//
// Emits a .hpp/.cpp pair per grammar module via a Writer (hardcoded
// recursive-descent generation — no template files).
//
// Generated file pair for a grammar named "sql":
//
//   sql_parser.hpp  ─── NamedCode constants + Parser class declaration
//   sql_parser.cpp  ─── recursive descent methods + Pratt expression parser
//
// Generated parser interface (example for grammar named "writ"):
//
//   namespace logos::writ {
//
//   // Field keys and node codes from %fields / %nodes
//   namespace writ_ast {
//       using Key  = logos::NamedCode<uint8_t>;
//       using Code = logos::NamedCode<int32_t>;
//       inline constexpr Key  NAME  {"NAME",  0};
//       inline constexpr Code MAP   {"MAP",   1};
//       ...
//   }
//
//   class WritParser {
//   public:
//       explicit WritParser(std::string_view source);
//
//       // One method per %export-ed rule.
//       logos::writ::Writ parse_value();
//       logos::writ::Writ parse_map();
//
//   private:
//       // Generated rule methods (one per grammar rule).
//       void* rule_value();
//       void* rule_map();
//       ...
//
//       // Pratt expression parser (generated from %prec table).
//       void* pratt_expr(int min_prec = 0);
//
//       // Lexer state.
//       std::string_view source_;
//       size_t pos_;
//       ...
//   };
//
//   } // namespace logos::writ

#pragma once

#include <string>
#include <filesystem>
#include "module_resolver.hpp"

namespace logos::peg_gen {

struct CodegenOptions {
    std::filesystem::path output_dir;  // where to write generated files
    bool                  overwrite = true;
    // When set, the ROOT grammar's %fields/%nodes are ALSO emitted as a
    // standalone C++ constants header at this path. That header used to be
    // hand-maintained beside the grammar (include/logos/compiler/ast.hpp) —
    // two declarations of the same integers with nothing checking them. A
    // name/value mismatch there is a silent runtime bug, and one had already
    // happened (WRIT_BLOB and TEMPLATE_DECL both claimed code 199).
    std::filesystem::path ast_header;
};

// Generate C++ parser files for all resolved modules.
// Each module gets its own .hpp/.cpp pair in output_dir.
void codegen(const std::vector<ResolvedModule>& modules, const CodegenOptions& opts);

} // namespace logos::peg_gen
