// Logos project — https://github.com/victor-smirnov/logos
//
// Monomorphization pass declaration.

#pragma once

#include <logos/compiler/lir.hpp>
#include <logos/compiler/str_map.hpp>

namespace logos::compiler {

// M3 step 3: full definition lives in src/compiler/module_loader.hpp.
// Held in MonoOpts as a non-owning const pointer so this header (in
// include/) doesn't need to chase the loader-side type.
struct StdlibExports;

// Run monomorphization on a fully sema-lowered program.
// Generic functions are instantiated for each unique set of type arguments
// found in call sites.  The returned program contains no TypeVar types.
//
// max_instantiation_depth limits recursive generic instantiation (default 64).
//
// Opts.entry_points (optional): when non-empty, only fns reachable from these
// names get cloned/instantiated. Used by the metaprog/metacall JIT compile
// paths to avoid processing the whole stdlib for tiny hook modules. Empty
// (default) preserves the old eager behaviour — every non-generic free fn
// is processed.
struct MonoOpts {
    int    max_instantiation_depth = 64;
    StrSet entry_points;   // empty → all non-generic free fns are roots
    // M6.2: when non-empty, mono extends `prev_out` instead of starting
    // from a fresh LProgram. Used by run_metaprog_dispatch so iter N+1's
    // mono skips the re-cloning that iter N already did (its concrete
    // instances seed the done_/struct_done_/enum_done_ tracking sets).
    // The two LPrograms must share the same TypePool/pools (via SemaCache);
    // mono moves prev_out into its out_ at start of run.
    lir::LProgram prev_out;
    // M3 step 3: stdlib template catalog decoded from .hermes0 v3 trailers.
    // Non-owning; the caller (main.cpp / emit_module) keeps the value alive
    // for the duration of mono_pass. Empty/null = no exports available
    // (cold-build / non-stdlib-using compile / older v2 archives only).
    // Mono uses it to skip indexing work for stdlib content once the
    // user-side hookup lands (a later M3 step); currently stored only.
    const StdlibExports* stdlib_exports = nullptr;
};

lir::LProgram mono_pass(lir::LProgram prog, int max_instantiation_depth = 64);
lir::LProgram mono_pass(lir::LProgram prog, MonoOpts opts);

} // namespace logos::compiler
