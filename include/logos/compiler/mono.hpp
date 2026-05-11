// Logos project — https://github.com/victor-smirnov/logos
//
// Monomorphization pass declaration.

#pragma once

#include <logos/compiler/lir.hpp>

namespace logos::compiler {

// Run monomorphization on a fully sema-lowered program.
// Generic functions are instantiated for each unique set of type arguments
// found in call sites.  The returned program contains no TypeVar types.
//
// max_instantiation_depth limits recursive generic instantiation (default 64).
lir::LProgram mono_pass(lir::LProgram prog, int max_instantiation_depth = 64);

} // namespace logos::compiler
