// Logos project — https://github.com/victor-smirnov/logos
//
// grammar_parser: hand-written recursive descent parser for .peg files.
//
// Produces a Hermes document conforming to the grammar_ast.hpp schema.
//
// ── .peg file format ────────────────────────────────────────────────────────
//
//   // Line comments.
//
//   %meta {
//       name:      "hermes"
//       version:   "1.0"
//       namespace: "logos::hermes"
//       output:    "hermes_parser"     // base name for generated files
//   }
//
//   %import "other.peg" as other       // grammar module import
//
//   %export { rule1 rule2 }            // public API of this grammar module
//
//   %fields {                          // → NamedCode<uint8_t> constants
//       NAME  = 0
//       VALUE = 1
//   }
//
//   %nodes {                           // → NamedCode<int32_t> constants
//       MAP   = 0
//       ARRAY = 1
//   }
//
//   %tokens {
//       SELECT   = "SELECT"            // keyword literal (case-sensitive)
//       IDENT    = /[a-zA-Z_]\w*/     // regex pattern
//       %skip    = /[ \t\n\r]+/       // whitespace (not emitted as tokens)
//       %skip    = /\/\/[^\n]*/       // line comment skip
//   }
//
//   %prec {                            // operator precedence (low → high)
//       left:  OR
//       left:  AND
//       right: NOT
//       left:  PLUS MINUS
//       left:  STAR SLASH
//   }
//
//   %rules {
//       // PEG rule: alternatives separated by /
//       value <- map
//              / STRING  => { CODE: STRING_NODE, VALUE: $1 }
//              / INTEGER => { CODE: INT_NODE,    VALUE: $1 }
//
//       // Sequence: items separated by spaces
//       map <- LBRACE entry* RBRACE
//           => { CODE: MAP_NODE, ITEMS: $... }
//
//       // Cross-grammar reference (imported as "sql")
//       typed_value <- sql::expr COLON value
//
//       // Grouping, optional, repetition
//       list <- LBRACKET (value (COMMA value)*)? RBRACKET
//   }

#pragma once

#include <optional>
#include <string>
#include <logos/hermes/view.hpp>

namespace logos::peg_gen {

// Parse a .peg grammar file into a Hermes document (grammar_ast schema).
// Returns nullopt and prints error to stderr on failure.
std::optional<logos::hermes::Hermes> parse_grammar(const std::string& path);

// Parse grammar from an in-memory string (useful for tests).
// source_name is used in error messages.
std::optional<logos::hermes::Hermes>
parse_grammar_string(std::string_view source, std::string_view source_name = "<string>");

} // namespace logos::peg_gen
