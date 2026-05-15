// metaprog dispatch loop — JIT-fires #[derive_*] / handler hooks and
// processes item-position metacalls, splicing synthesised items into
// the asts vector. Shared between the user-facing main path
// (logosc <foo.logos>) and the stdlib build path (logosc --emit-module).
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <logos/hermes/view.hpp>  // hermes::Hermes (= Own<HermesView>)
#include <logos/compiler/str_map.hpp>

namespace logos::compiler {

// Per-emitted-doc provenance tracked when --dump-metaprog is on.
// Sized parallel to `asts` (sparse; entries stay nullopt for docs
// that weren't emitted by a metafn).
struct EmitProvenance {
    std::string  src_file;     // file containing the metacall/trigger site
    int          src_line = 0; // 1-based line; 0 = unknown
    std::string  callee_name;  // metacall callee, or hook fn name (for triggers)
    std::string  trigger;      // empty for `metacall`; #[derive_clone]→"derive_clone"
    std::string  target_name;  // for triggers: name of the annotated item
                               // (struct/fn/etc.); empty for `metacall`
    int          iter_seq = 0; // metaprog discovery-loop iteration (informational)
};

struct MetaprogDispatchOpts {
    bool                     trace = false;
    std::string              dump_dir;       // --dump-metaprog: empty disables
    std::string              dump_filter;    // optional callee/file filter
    std::vector<std::string> archive_paths;  // .a files for JIT lookup (Mode B)
    // Optional output: when non-null + dump_dir non-empty, the dispatcher
    // appends provenance entries for each metaprog-emitted AST. Caller
    // owns the vector and reads it after dispatch returns.
    std::vector<std::optional<EmitProvenance>>* provenance_out = nullptr;
    // Phase 2-4: cfg flags propagated to every sema_lower call inside the
    // dispatch loop. Each entry is `feature=name` or a bare flag.
    std::vector<std::string> cfg_flags;
    // Symbols already provided by the JIT's archive_paths (and the final
    // link archives) — copy onto meta_prog.binary_symbols so mlir_gen
    // skips body emission for fns whose pre-baked impl is in those .a
    // files. Empty disables the skip (legacy behaviour).
    StrSet binary_symbols;
};

// Run the metaprog discovery loop:
//   - sema_lower in metaprog_mode (entry-file fn bodies skipped)
//   - JIT-invoke each #[derive_*]/metaprog-handler trigger; hooks may
//     append items to `asts` via logos_emit_item_blob_subst
//   - process item-position metacall thunks (`metacall foo();` items)
//   - iterate until no new emissions, capped at 16
//
// Returns 0 on success, !=0 on failure (diagnostics printed to stderr).
//
// Sets g_asts/g_filenames/g_from_binary/g_user_root_idx/g_emit_seen/
// g_any_emitted/g_metaprog_diags/g_ast_provenance globals from the
// args; restores prior values on exit. Caller need only own the
// asts/filenames/from_binary vectors (which may grow).
int run_metaprog_dispatch(
    std::vector<hermes::Hermes>& asts,
    std::vector<std::string>&    filenames,
    std::vector<bool>&           from_binary,
    std::size_t                  entry_ast_idx,
    const MetaprogDispatchOpts&  opts);

} // namespace logos::compiler
