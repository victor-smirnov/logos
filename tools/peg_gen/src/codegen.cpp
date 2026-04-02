// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov

#include "codegen.hpp"
#include "grammar_ast.hpp"

#include <print>

// Code generation uses HermesTemplate to render C++ from .htpl files.
// Each grammar module produces two files:
//   <output>_parser.hpp  — NamedCode constants + Parser class declaration
//   <output>_parser.cpp  — rule methods + Pratt expression parser
//
// Template context passed to HermesTemplate:
//   The grammar Hermes document itself — templates access grammar fields
//   directly (meta.namespace, rules, fields, nodes, etc.)
//
// Template files:
//   templates/parser.hpp.htpl
//   templates/parser.cpp.htpl

namespace logos::peg_gen {

void codegen(const std::vector<ResolvedModule>& modules, const CodegenOptions& opts) {
    // TODO: implement
    //   For each module in topological order:
    //     1. Load parser.hpp.htpl and parser.cpp.htpl from opts.templates_dir
    //     2. Build template context from module.grammar document
    //     3. Add imported module names to context (for #include generation)
    //     4. Render via HermesTemplate
    //     5. Write to opts.output_dir / (output_name + "_parser.hpp/.cpp")
    std::println(stderr, "peg_gen: codegen not yet implemented ({} module(s))", modules.size());
}

} // namespace logos::peg_gen
