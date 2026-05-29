// Logos project — https://github.com/victor-smirnov/logos
//
// Phase-1 borrow checker: linear ownership (use-after-move detection).
//
// Runs after monomorphization (all types concrete) and before mlir_gen.
// Adds errors to LProgram::diags; callers check prog.ok() as usual.

#pragma once

#include <logos/compiler/lir.hpp>

namespace logos::compiler {

// Run ownership analysis on every concrete function in prog.
// Returns the same prog (with any errors appended to prog.diags).
// `generic_templates_only`: P2-10 — when true, check ONLY generic fn templates
// (pre-mono), in exclusivity-only mode (move tracking is imprecise on TypeVars).
// Default false = the normal post-mono pass over concrete fns + specializations.
lir::LProgram borrow_check(lir::LProgram prog, bool generic_templates_only = false);

} // namespace logos::compiler
