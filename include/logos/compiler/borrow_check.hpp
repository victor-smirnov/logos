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
lir::LProgram borrow_check(lir::LProgram prog);

} // namespace logos::compiler
