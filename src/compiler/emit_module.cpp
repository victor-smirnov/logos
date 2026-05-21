//
// emit_module — build a binary Logos module (.a archive) from a manifest.
//
// Output: libNAME.a containing
//   NAME.o       — compiled non-generic code for the whole module
//   NAME.hermes0 — binary AST dump (for sema on client side)

#include "emit_module.hpp"
#include "compile_pipeline.hpp"
#include "metaprog_dispatch.hpp"
#include "module_loader.hpp"

#include <logos/compiler/ast.hpp>

#include <climits>
#include <unistd.h>

#include <logos/compiler/borrow_check.hpp>
#include <logos/compiler/lir.hpp>
#include <logos/compiler/mono.hpp>
#include <logos/hermes/arena_publish.hpp>
#include <logos/hermes/binary_codec.hpp>
#include <logos/hermes/clone.hpp>
#include <logos/hermes/document.hpp>
#include <logos/hermes/import_table.hpp>
#include <logos/hermes/type_ops.hpp>

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <unordered_set>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace logos::compiler {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// .hermes0 format (version 3)
//
//   magic[8]      "HERMAST0"
//   version       uint32_t  = 3
//   num_files     uint32_t
//   for each file:
//     path_len    uint32_t
//     path        char[path_len]
//     pkg_len     uint32_t
//     pkg         char[pkg_len]   — dotted package name (e.g. "std.io")
//     ast_len     uint64_t
//     ast         uint8_t[ast_len]  (binary_codec output)
//   // M3 (version 3 addition):
//   exports_len   uint64_t          — bytes following; 0 = empty/no exports
//   exports       uint8_t[exports_len]
//                                   — see StdlibExports in module_loader.hpp
//                                     for the inner trailer format. Loaders
//                                     that don't know a given trailer_version
//                                     safely skip.
//   // M4 step 1 (no version bump — optional trailing section):
//   lir_blob_len  uint64_t          — bytes following; 0 = empty/no blob
//   lir_blob      uint8_t[lir_blob_len]
//                                   — raw bytes of mono's post-mono
//                                     prog.type_pool.arena() head chunk.
//                                     Includes DocumentHeader at offset 0.
//                                     Load via hermes::from_bytes_copy. The
//                                     blob holds the LIR Hermes mirror
//                                     (LStructDef / LFunction / LExpr / …)
//                                     so user-side sema/mono can skip
//                                     re-lowering stdlib AST once the
//                                     register-pre-lowered path lands
//                                     (later M4 steps).
//
// v2 readers fail on v3; v3 readers accept both v2 and v3 (the file table
// layout is identical between versions, only the trailing sections are new
// in v3). Trailing sections beyond the exports trailer are read lazily —
// readers that don't know about them (M3-era archives written before this
// commit) simply have no bytes after exports and the reader stops cleanly.
// ---------------------------------------------------------------------------

static void write_u32(std::ofstream& f, uint32_t v) {
    f.write(reinterpret_cast<const char*>(&v), 4);
}
static void write_u64(std::ofstream& f, uint64_t v) {
    f.write(reinterpret_cast<const char*>(&v), 8);
}

// Build the inner trailer bytes (trailer_version=2) from a StdlibExports
// value. Length-prefixed primitives; see the StdlibExports header comment
// in module_loader.hpp for the exact byte layout.
static std::string encode_stdlib_exports(const StdlibExports& exp) {
    std::string out;
    auto put_u16 = [&](uint16_t v) {
        out.append(reinterpret_cast<const char*>(&v), 2);
    };
    auto put_u32 = [&](uint32_t v) {
        out.append(reinterpret_cast<const char*>(&v), 4);
    };
    auto put_str = [&](const std::string& s) {
        put_u32(static_cast<uint32_t>(s.size()));
        out.append(s.data(), s.size());
    };
    put_u16(2);   // trailer_version (writer always emits the latest)
    put_u16(0);   // reserved
    // v1 payload (unchanged)
    put_u32(static_cast<uint32_t>(exp.struct_templates.size()));
    for (auto& [pkg, name] : exp.struct_templates) { put_str(pkg); put_str(name); }
    put_u32(static_cast<uint32_t>(exp.enum_templates.size()));
    for (auto& [pkg, name] : exp.enum_templates)   { put_str(pkg); put_str(name); }
    put_u32(static_cast<uint32_t>(exp.fn_templates.size()));
    for (auto& name : exp.fn_templates)            { put_str(name); }
    // v2 additions
    put_u32(static_cast<uint32_t>(exp.blanket_impls.size()));
    for (auto& bi : exp.blanket_impls) {
        put_str(bi.trait_name);
        put_str(bi.bound_trait);
        put_u32(static_cast<uint32_t>(bi.extra_bounds.size()));
        for (auto& b : bi.extra_bounds) put_str(b);
    }
    put_u32(static_cast<uint32_t>(exp.concrete_impls.size()));
    for (auto& ci : exp.concrete_impls) {
        put_str(ci.trait_name);
        put_str(ci.target_type);
    }
    return out;
}

// Phase 6 (multi-arena IR): module-level flags stored as a trailing u64
// after lir_blob. Old (v3) readers stop after lir_blob — they ignore this
// section and treat the archive as eager (the previous default), so
// adding the section is forward-compatible.
namespace module_flag {
    constexpr uint64_t LAZY = 1ULL << 0;
}

static bool write_hermes0(const std::string& path,
                           const std::vector<ParsedModule>& modules,
                           const StdlibExports* exports = nullptr,
                           const std::vector<uint8_t>* lir_blob = nullptr,
                           uint64_t module_flags = 0) {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "emit_module: cannot create %s\n", path.c_str());
        return false;
    }
    // header
    f.write("HERMAST0", 8);
    write_u32(f, 3);  // version 3: adds trailing exports section
    write_u32(f, static_cast<uint32_t>(modules.size()));

    for (auto& mod : modules) {
        auto enc = hermes::binary_encode(mod.ast);
        if (!enc) {
            std::fprintf(stderr, "emit_module: binary_encode failed for %s\n",
                         mod.path.c_str());
            return false;
        }
        uint32_t path_len = static_cast<uint32_t>(mod.path.size());
        write_u32(f, path_len);
        f.write(mod.path.data(), path_len);
        uint32_t pkg_len = static_cast<uint32_t>(mod.package.size());
        write_u32(f, pkg_len);
        f.write(mod.package.data(), pkg_len);
        uint64_t ast_len = enc->size();
        write_u64(f, ast_len);
        f.write(reinterpret_cast<const char*>(enc->data()), ast_len);
    }
    // M3 step 2: exports trailer. Empty `exports` writes a 0 length-prefix
    // (interpreted by readers as "no exports"); a non-null `exports`
    // serializes the StdlibExports payload.
    if (exports) {
        std::string payload = encode_stdlib_exports(*exports);
        write_u64(f, static_cast<uint64_t>(payload.size()));
        f.write(payload.data(), payload.size());
    } else {
        write_u64(f, 0);
    }
    // M4 step 1: optional LIR mirror blob. Always emit the u64 length so
    // readers can distinguish "no blob" (0) from "section absent" (EOF
    // before the u64). Older archives written before M4 step 1 have no
    // bytes after exports — the loader's "8+ bytes remaining" check
    // handles both shapes.
    if (lir_blob) {
        write_u64(f, static_cast<uint64_t>(lir_blob->size()));
        f.write(reinterpret_cast<const char*>(lir_blob->data()),
                lir_blob->size());
    } else {
        write_u64(f, 0);
    }
    // Phase 6: module-flags trailing u64. Omit when zero so old archives
    // and the eager-default case stay byte-identical to pre-Phase-6
    // output (this section is recognised only when non-zero is needed).
    if (module_flags) {
        write_u64(f, module_flags);
    }
    return f.good();
}

// ---------------------------------------------------------------------------
// Collect all .logos files under a directory (recursive).
// ---------------------------------------------------------------------------
static std::vector<std::string> collect_logos_files(const std::string& root) {
    std::vector<std::string> result;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(
             root, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator();
         it.increment(ec))
    {
        if (!it->is_regular_file(ec)) continue;
        if (it->path().extension() != ".logos") continue;
        result.push_back(fs::weakly_canonical(it->path(), ec).string());
    }
    std::sort(result.begin(), result.end());
    return result;
}

// ---------------------------------------------------------------------------
// Compile a set of parsed modules to an object file.
// Returns true on success.
// ---------------------------------------------------------------------------
// Filter codegen body emission to one source file when `only_file` is set:
// every fn whose source_file != only_file is added to prog.binary_symbols
// so mlir_gen forward-declares it without emitting a body. The linker
// resolves these symbols at archive-merge time from sibling per-file .o
// files produced by other --emit-file invocations.
static void apply_only_file_filter(lir::LProgram& prog,
                                    const std::string& only_file) {
    if (only_file.empty()) return;
    auto fits = [&](const std::string& src) {
        if (src == only_file) return true;
        // Tolerate path-shape mismatches by also matching on the lexical
        // suffix (e.g. relative vs canonical paths).
        if (src.size() >= only_file.size() &&
            src.compare(src.size() - only_file.size(), only_file.size(), only_file) == 0)
            return true;
        if (only_file.size() >= src.size() &&
            only_file.compare(only_file.size() - src.size(), src.size(), src) == 0)
            return true;
        return false;
    };
    auto add = [&](const lir::LFunction& fn) {
        if (fn.is_extern) return;
        if (fits(fn.source_file)) return;
        prog.binary_symbols.insert(fn.name);
    };
    for (auto& sd : prog.structs)
        for (auto& m : sd.methods) add(*m);
    for (auto& fn : prog.functions) add(*fn);
}

static bool compile_to_object(std::vector<hermes::Hermes>& asts,
                               std::vector<std::string>& filenames,
                               const std::vector<bool>& ast_only_flags,
                               const std::vector<bool>& from_binary_module_flags,
                               const std::string& obj_path,
                               const std::string& only_file = "",
                               StdlibExports* out_exports = nullptr,
                               std::vector<uint8_t>* out_lir_blob = nullptr,
                               const std::string& module_name = "",
                               const std::string& implicit_prelude = "",
                               const std::vector<std::string>& dep_archives = {}) {
    // Run metaprog discovery loop (#21 closure) so #[derive_*] hooks
    // and metacall thunks fire during stdlib build. asts/filenames
    // grow with synthesised docs that subsequent sema picks up.
    //
    // ast_only modules participate in dispatch (their handler fns
    // need to JIT-compile + register triggers) but get from_binary=
    // true for the post-dispatch sema pass — host externs in their
    // bodies (logos_emit_*, etc.) make them unsuitable for codegen.
    //
    // Three-layer split fix: pre-stamp from_binary with the
    // ParsedModule::from_binary_module flag BEFORE dispatch runs sema.
    // Without this, the dispatch sema pass registers binary-loaded
    // traits/structs with from_binary=false; the lir_bundle filter
    // at sema.cpp:489 then drops them from the binary cache (which
    // keeps only from_binary=true entries), so the second
    // post-dispatch sema starts with an empty trait registry for
    // binary-sourced packages and emits "unknown trait" diagnostics
    // for cross-archive impls.
    std::vector<bool> from_binary(asts.size(), false);
    for (size_t i = 0; i < from_binary.size(); ++i) {
        if (i < from_binary_module_flags.size() && from_binary_module_flags[i])
            from_binary[i] = true;
    }

    // Collect the `nm --defined-only` symbol set of the dependency archives
    // ONCE, up front. It's the skeleton-skip gate (sema skips lowering bodies
    // of from_binary fns already compiled into a dep .o) AND the codegen
    // forward-declare gate (mlir_gen's is_binary_skip keys on
    // prog.binary_symbols). Collected before sema so the gate is populated by
    // the time bodies are lowered. (Was collected post-sema — too late for the
    // sema gate, which previously relied on the LIR-blob lookup_export proxy.)
    StrSet dep_symbols;
    for (const auto& a : dep_archives) {
        FILE* pipe = ::popen(("nm --defined-only -j " + a + " 2>/dev/null").c_str(), "r");
        if (!pipe) continue;
        char line[512];
        while (std::fgets(line, sizeof(line), pipe)) {
            std::string_view sv(line);
            while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r' || sv.back() == ' '))
                sv.remove_suffix(1);
            if (!sv.empty() && sv.front() != '/')
                dep_symbols.emplace(sv);
        }
        ::pclose(pipe);
    }

    {
        MetaprogDispatchOpts mopts;
        mopts.binary_symbols = dep_symbols;  // skeleton-skip gate for dispatch sema
        // Stdlib build chicken-and-egg: dispatch needs to JIT-compile
        // handler fns whose bodies reach into stdlib (Vec, AnyVal, etc.).
        // The metaprog mlir module spans the whole stdlib, so JIT lookup
        // of the hook fn transitively materializes deep stdlib symbols
        // including externs (clock_gettime, IoRing, etc.) that aren't
        // bound in JIT setup. Resolve those via the *prior* build's
        // liblstdlib*.a (still on disk at dispatch time — emit_module
        // overwrites only after compile_to_object returns).
        //
        // Cold-build limitation: when no prior .a exists (first build,
        // clean tree), this falls through and dispatch fails on the
        // first derive trigger that demands stdlib symbol resolution.
        // Bootstrap: build once without stdlib-side derives, then with.
        for (auto* env_var : {"LOGOS_LIB_DIR"}) {
            if (const char* dir = std::getenv(env_var)) {
                fs::path d(dir);
                std::error_code dec;
                if (fs::is_directory(d, dec)) {
                    for (auto& ent : fs::directory_iterator(d, dec)) {
                        if (!ent.is_regular_file()) continue;
                        auto fn = ent.path().filename().string();
                        if ((fn.rfind("liblstdlib", 0) == 0 ||
                             fn.rfind("liblogos-", 0) == 0) &&
                            ent.path().extension() == ".a") {
                            mopts.archive_paths.push_back(ent.path().string());
                        }
                    }
                }
                break;
            }
        }
        // Fallback: argv[0]-relative. Mirrors main.cpp's resolve_system_lib_dir.
        if (mopts.archive_paths.empty()) {
            char exe[PATH_MAX];
            ssize_t n = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
            if (n > 0) {
                exe[n] = '\0';
                std::string p(exe);
                if (auto slash = p.rfind('/'); slash != std::string::npos)
                    p.resize(slash);
#ifdef LOGOS_LIB_RELDIR
                p += "/" LOGOS_LIB_RELDIR;
#else
                p += "/../lib/logos";
#endif
                char real[PATH_MAX];
                if (::realpath(p.c_str(), real)) {
                    fs::path d(real);
                    std::error_code dec;
                    if (fs::is_directory(d, dec)) {
                        for (auto& ent : fs::directory_iterator(d, dec)) {
                            if (!ent.is_regular_file()) continue;
                            auto fn = ent.path().filename().string();
                            if ((fn.rfind("liblstdlib", 0) == 0 ||
                                 fn.rfind("liblogos-", 0) == 0) &&
                                ent.path().extension() == ".a") {
                                mopts.archive_paths.push_back(ent.path().string());
                            }
                        }
                    }
                }
            }
        }
        // emit_module bundles N files — there's no single "entry"; use a
        // sentinel so the entry-only metaprog body-skip never matches an
        // actual ast index. (Was: asts.size()-1, which silently stubbed
        // free-fn bodies in whichever ast happened to load last —
        // typically std.time.logos — corrupting metaprog mlir gen.)
        size_t entry_idx = static_cast<size_t>(-1);
        if (run_metaprog_dispatch(asts, filenames, from_binary, entry_idx, mopts) != 0)
            return false;
    }
    // Re-stamp from_binary: ast_only files become "binary" so the
    // post-dispatch sema/mono/mlir-gen pass treats them as already-
    // emitted (no codegen for fns that bind to host externs). Synth
    // docs appended by dispatch (filename "<metaprog-blob-subst>" /
    // "<metaprog>") stay non-binary so their items get lowered.
    from_binary.assign(asts.size(), false);
    for (size_t i = 0; i < from_binary.size(); ++i) {
        bool ao = (i < ast_only_flags.size()) && ast_only_flags[i];
        // Three-layer split fix: also propagate the ParsedModule
        // from_binary_module flag (modules loaded from a .a archive's
        // .hermes0 member). Pre-fix, this was silently set to false →
        // sema_collect's cur_from_binary_ never tripped for archive-
        // sourced traits/structs → the binary-cache filter
        // (only_binary_vec at sema.cpp:489) dropped them → user
        // sema saw empty traits_/structs_/... and emitted
        // "unknown trait / unknown struct" diagnostics for
        // cross-archive references.
        bool bm = (i < from_binary_module_flags.size())
                  && from_binary_module_flags[i];
        from_binary[i] = ao || bm;
    }
    // Sema — all files in the module are being compiled from source.
    // Layer .hermes0 dedup: keep blob skeletons ON so `depends`-archive
    // modules are skeleton-skipped (referenced cross-arena, not re-embedded
    // in this layer's LIR blob). Two guards keep it correct for a PRODUCER
    // build (vs the consumer fast-path):
    //  - lookup_export-gated (sema_decl.cpp): ast_only same-build files miss
    //    the lookup → keep full bodies so mono's scan_fn sees their generics.
    //  - blob_skip_nongeneric_only: generic templates keep local bodies so
    //    mono instantiates them here directly, avoiding the producer-side
    //    cross-arena clone (which can emit wrong-layout instantiations).
    SemaOptions sema_opts;
    sema_opts.blob_skip_nongeneric_only = true;
    sema_opts.implicit_prelude = implicit_prelude;
    sema_opts.binary_symbols = dep_symbols;  // skeleton-skip gate
    auto prog = sema_lower(asts, filenames, from_binary, sema_opts);
    prog.print_diags(stderr);
    if (!prog.ok()) return false;

    // Phase 5.B: snapshot generic template names + body mirror offsets
    // BEFORE mono_pass moves prog into its in_. Mono drops templates after
    // instantiation (they're not in post-mono out_), but their body mirrors
    // remain at stable offsets in the arena. Capture (name, body_offset)
    // here and publish in the post-mono publish phase below.
    // Pre-Phase-5.B: this snapshot was used only for the M3 stdlib-exports
    // catalog (struct/enum/fn template names + impl catalog); now extended
    // with body offsets so the multi-arena EXPORTS map can resolve template
    // names → arena_offset for cross-arena clone.
    struct TemplateEntry {
        std::string                name;
        hermes::arena_offset_t     body_offset{};
    };
    std::vector<TemplateEntry> generic_fn_templates;
    std::vector<TemplateEntry> generic_method_templates;
    auto stash_template = [](std::vector<TemplateEntry>& dst, const lir::LFunction& fn) {
        if (fn.is_extern) return;
        // Phase 5.B step 3 (Phase 5.C close-out): the prior
        // `if (fn.from_binary_module) return` here was wrong during the
        // stdlib build for ast_only files (e.g. std.compiler.metaprog) —
        // those get stamped from_binary=true post-metaprog-dispatch but
        // their bodies came from THIS sema's source, so there's no other
        // archive providing them. The mirror_offset_ guard below catches
        // the genuine "already published" case (mirror missing means
        // body never lowered locally → nothing to publish).
        if (fn.body.mirror_offset_ == hermes::arena_offset_t{}) return;
        dst.push_back({fn.name, fn.body.mirror_offset_});
    };
    for (auto& fn : prog.functions) {
        if (fn && !fn->type_params.empty()) stash_template(generic_fn_templates, *fn);
    }
    for (auto& sd : prog.structs) {
        if (sd.type_params.empty()) continue;
        for (auto& m : sd.methods) {
            if (m) stash_template(generic_method_templates, *m);
        }
    }

    // M3 step 2+4: snapshot generic template names + blanket/concrete impl
    // catalog from the post-sema LProgram BEFORE mono_pass moves prog into
    // its in_. Template criterion: `type_params` non-empty (mirrors mono's
    // own indexing logic in mono.cpp:133/195/207). Impl catalog excludes
    // negative impls (`impl !Trait`) — they're sema-level filters with no
    // runtime presence.
    if (out_exports) {
        for (auto& sd : prog.structs)
            if (!sd.type_params.empty())
                out_exports->struct_templates.push_back({sd.pkg, sd.name});
        for (auto& ed : prog.enums)
            if (!ed.type_params.empty())
                out_exports->enum_templates.push_back({ed.pkg, ed.name});
        for (auto& fn : prog.functions)
            if (!fn->type_params.empty())
                out_exports->fn_templates.push_back(fn->name);
        for (auto& impl : prog.impls) {
            if (impl.is_negative) continue;
            if (impl.is_blanket) {
                out_exports->blanket_impls.push_back({
                    impl.trait_name, impl.bound_trait, impl.extra_bounds});
            } else {
                out_exports->concrete_impls.push_back({
                    impl.trait_name, impl.target_type});
            }
        }
    }

    // Mono (also emits L-IR Hermes mirror; borrow_check reads via mirror)
    prog = mono_pass(std::move(prog));
    prog.print_diags(stderr);
    if (!prog.ok()) return false;

    // Borrow check
    prog = borrow_check(std::move(prog));
    prog.print_diags(stderr);
    if (!prog.ok()) return false;

    // M4 step 1: snapshot prog.type_pool arena bytes for the .hermes0 LIR
    // blob section. This is the post-mono LIR Hermes mirror — every
    // template/struct/fn/expr/stmt that mono produced lives here with its
    // mirror_offset_ value referencing offsets in these very bytes. Loaded
    // user-side via hermes::from_bytes_copy; future M4 steps add the cross-
    // arena lookup so sema/mono skip re-lowering stdlib AST.
    //
    // Done before apply_only_file_filter so per-file-mode invocations still
    // see a complete blob (the filter only narrows codegen, not the mirror
    // — and stdlib build never uses --only-file anyway).
    if (out_lir_blob) {
        if (auto* arena = prog.type_pool.arena()) {
            // Multi-arena IR Phase 3: publish a LirArenaRoot into the arena
            // so the dumped blob ships with a proper multi-arena entry
            // point (DocumentHeader.root_offset → LirArenaRoot). Loader
            // calls register_lir_arena(doc) after from_bytes_copy to wire
            // this arena into the global ArenaPool.
            //
            // Directory is empty in Phase 3 (only the null sentinel at
            // obj_id 0); Phase 4 will populate it with template body
            // offsets so sema can skip re-lowering stdlib AST.
            //
            // module_name == "" path: legacy emit; skip publish so existing
            // tests / per-file emit (which don't have a manifest name)
            // still produce valid M4-step-1 blobs without LirArenaRoot.
            if (!module_name.empty()) {
                auto* holder = prog.type_pool.holder();
                if (holder) {
                    auto doc = hermes::Hermes(hermes::HermesView(holder));
                    if (auto bld = hermes::lir_arena_root_begin(
                            doc, module_name, /*deps=*/{})) {
                        // Phase 4.B: publish each non-generic, non-extern,
                        // non-specialization fn body whose mirror was emitted
                        // into this arena. Sema's skeleton path looks the
                        // name up in EXPORTS to resolve body_external_ref.
                        //
                        // What we publish is the LBlock mirror offset (the
                        // fn body). Consumer resolves directory[obj_id] →
                        // arena_offset → BlockRef. Struct methods follow
                        // the same rule, gated on the struct being non-
                        // generic itself (mono can't clone a method whose
                        // struct template is not yet instantiated).
                        size_t published = 0, published_tmpl = 0;
                        // Multi-arena step 1: stamp each published element's
                        // body map with its linear obj_id as EXPORT_ID, so the
                        // element self-identifies for cross-arena references
                        // (the stable handle that replaces name lookup).
                        // TinyObjectMap::put is offset-stable — the map header
                        // stays at its offset; only its values array may move —
                        // so stamping after the mirror was emitted is safe.
                        auto stamp_export_id =
                            [&](hermes::arena_offset_t off, uint32_t oid) {
                            if (off == hermes::arena_offset_t{}) return;
                            auto* base = arena->head().data();
                            auto* m = reinterpret_cast<hermes::TinyObjectMap*>(
                                base + off.value());
                            auto av = hermes::AnyVal::from_value<uint32_t>(
                                oid, static_cast<uint8_t>(hermes::type_hash::U24));
                            (void) m->put(lir_schema::stmt_keys::EXPORT_ID.code, av, *arena);
                        };
                        auto try_publish = [&](const lir::LFunction& fn) {
                            if (fn.is_extern) return;
                            if (fn.is_specialization) return;
                            if (!fn.type_params.empty()) return;
                            if (fn.from_binary_module) return;
                            if (fn.body.mirror_offset_ == hermes::arena_offset_t{}) return;
                            auto av = hermes::AnyVal::from_offset(fn.body.mirror_offset_);
                            if (auto r = hermes::arena_publish_named(*bld, fn.name, av)) {
                                stamp_export_id(fn.body.mirror_offset_, *r);
                                ++published;
                            }
                        };
                        for (auto& fn : prog.functions) {
                            if (fn) try_publish(*fn);
                        }
                        for (auto& sd : prog.structs) {
                            if (!sd.type_params.empty()) continue;  // generic struct
                            for (auto& m : sd.methods) {
                                if (m) try_publish(*m);
                            }
                        }
                        // Phase 5.B: also publish generic template bodies (free
                        // fns + generic-struct methods). Mono dropped them from
                        // post-mono prog, but their mirror offsets stayed valid
                        // in the arena (captured above into generic_*_templates).
                        // Consumer = future mono cross-arena clone path: when a
                        // user-side mono is asked to instantiate Vec<MyType>, it
                        // looks up "Vec<T>::push" in EXPORTS, resolves to the
                        // template's body in stdlib's arena, walks via lir_view
                        // through that arena, substitutes into user's arena.
                        for (auto& tmpl : generic_fn_templates) {
                            auto av = hermes::AnyVal::from_offset(tmpl.body_offset);
                            if (auto r = hermes::arena_publish_named(*bld, tmpl.name, av)) {
                                stamp_export_id(tmpl.body_offset, *r);
                                ++published_tmpl;
                            }
                        }
                        for (auto& tmpl : generic_method_templates) {
                            auto av = hermes::AnyVal::from_offset(tmpl.body_offset);
                            if (auto r = hermes::arena_publish_named(*bld, tmpl.name, av)) {
                                stamp_export_id(tmpl.body_offset, *r);
                                ++published_tmpl;
                            }
                        }
                        if (std::getenv("LOGOS_TRACE_PHASES")) {
                            std::fprintf(stderr,
                                "emit_module: published %zu non-generic + %zu "
                                "template body export(s) for module '%s'\n",
                                published, published_tmpl, module_name.c_str());
                        }
                        auto fin = hermes::lir_arena_root_finalize(*bld);
                        (void) fin;
                    }
                }
            }

            const auto& chunk = arena->head();

            // A-LIR foundation: compactify the type-pool arena (reachability
            // GC from the LirArenaRoot) before dumping. The append-only arena
            // accumulates dead interned types that no published export
            // reaches; clone-from-root packs them out. Every mirror edge is an
            // AnyVal-offset pointer (no RelativePtr), so clone faithfully
            // remaps the LirArenaRoot + EXPORTS/DIRECTORY (incl. EXPORT_ID
            // stamps) and every body. Dep-type subgraphs stay reachable today
            // (own->dep offset edges intact) so they're still copied — cutting
            // those edges (TypeUID indirection) is the next A-LIR step, after
            // which this same clone drops them.
            //
            // Only when a LirArenaRoot was published (module_name set + finalize
            // ran); legacy/per-file emit without a root falls back to the raw
            // dump. On any clone failure we fall back rather than ship nothing.
            bool dumped = false;
            if (!module_name.empty()) {
                if (auto* h = prog.type_pool.holder()) {
                    auto src_view = hermes::HermesView(h);
                    if (auto cl = hermes::clone(src_view, nullptr)) {
                        const auto& cchunk = cl->holder()->arena().head();
                        out_lir_blob->assign(cchunk.data(),
                                             cchunk.data() + cchunk.used);
                        dumped = true;
                        if (std::getenv("LOGOS_TRACE_PHASES")) {
                            std::fprintf(stderr,
                                "emit_module: compactified LIR blob %zu -> %zu "
                                "bytes (%.2fx)\n",
                                (size_t)chunk.used, (size_t)cchunk.used,
                                cchunk.used ? double(chunk.used) / double(cchunk.used)
                                            : 0.0);
                        }
                    } else {
                        std::fprintf(stderr,
                            "emit_module: LIR compactify failed for '%s' — "
                            "falling back to raw arena dump\n",
                            module_name.c_str());
                    }
                }
            }
            if (!dumped)
                out_lir_blob->assign(chunk.data(), chunk.data() + chunk.used);
        }
    }

    // B1.7: per-file mode marks every fn outside the target file as
    // binary-skip so mlir_gen forward-declares without bodies.
    apply_only_file_filter(prog, only_file);

    // Layer dedup: forward-declare (don't re-emit) any function a `depends`
    // archive already defines. Each layer's .o then carries only its OWN
    // codegen + the generic instantiations mono produced for THIS layer's
    // types (those are absent from the lower archives, so they stay emitted).
    // Without this, mem.o re-embeds all of lang and std.o re-embeds lang+mem
    // (the old self-contained-archive scheme). is_binary_skip in mlir_gen
    // keys on prog.binary_symbols == the exact `nm`-defined symbols of the
    // deps, so a name match guarantees the linker (and the metacall JIT, which
    // now loads all layers) finds the body elsewhere. dep_symbols was already
    // collected up front (it doubles as the sema skeleton-skip gate).
    prog.binary_symbols.insert(dep_symbols.begin(), dep_symbols.end());

    // Shared lowering tail (mlir_gen → MLIR→LLVM → object).
    LowerEmitOpts lopts;
    lopts.opt_level = 0;            // stdlib build skips opt pipeline
    lopts.function_sections = true; // per-fn sections for --gc-sections
    return lower_and_emit_object(prog, obj_path, lopts) == 0;
}

// ---------------------------------------------------------------------------
// Resolve `depends X` manifest entries to absolute archive paths.
//
// Convention: `depends foo` looks for `libfoo.a` in search_paths (front-to-back,
// first hit wins). Matches the Unix `-lfoo` linker convention. Returns empty
// vector if any dependency cannot be resolved; err_out describes the first
// missing dep.
//
// This is the single integration point that turns a parsed-but-unused
// manifest field into something that drives the loader. Resolved paths are
// merged with EmitModuleOptions::extra_lib_files (depends prepended, so they
// load before any user-supplied -l files).
// ---------------------------------------------------------------------------
static std::vector<std::string>
resolve_manifest_depends(const ModuleManifest& manifest,
                         const std::vector<std::string>& search_paths,
                         std::string& err_out)
{
    std::vector<std::string> out;
    for (const auto& dep : manifest.depends) {
        std::string filename = "lib" + dep + ".a";
        bool found = false;
        for (const auto& dir : search_paths) {
            std::error_code ec;
            auto p = fs::path(dir) / filename;
            if (fs::exists(p, ec) && fs::is_regular_file(p, ec)) {
                out.push_back(fs::weakly_canonical(p, ec).string());
                found = true;
                break;
            }
        }
        if (!found) {
            err_out = "manifest 'depends " + dep + "': cannot find '" + filename
                    + "' in any search path";
            return {};
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------
bool emit_module(const ModuleManifest& manifest,
                 const std::string& output_path,
                 const EmitModuleOptions& opts)
{
    // Progress output is noisy on every build; gate it behind LOGOS_EMIT_VERBOSE=1.
    bool verbose = false;
    if (const char* v = std::getenv("LOGOS_EMIT_VERBOSE")) {
        verbose = (v[0] != '\0' && v[0] != '0');
    }

    // Resolve root relative to cwd.
    auto root = fs::weakly_canonical(manifest.root).string();
    if (verbose) {
        std::fprintf(stderr, "emit_module: building module '%s' from %s\n",
                     manifest.name.c_str(), root.c_str());
    }

    // Build search paths: module root + extra paths from -I flags.
    std::vector<std::string> search_paths = {root};
    for (auto& p : opts.extra_search_paths) search_paths.push_back(p);

    auto absolutize_prefixes = [&](const std::vector<std::string>& src) {
        std::vector<std::string> out;
        out.reserve(src.size());
        for (auto& e : src) {
            auto p = fs::path(root) / e;
            out.push_back(fs::weakly_canonical(p).string());
        }
        return out;
    };
    auto matches_any = [](const std::string& f, const std::vector<std::string>& prefixes) {
        for (auto& p : prefixes)
            if (f.compare(0, p.size(), p) == 0) return true;
        return false;
    };

    auto abs_excludes = absolutize_prefixes(manifest.excludes);
    auto abs_ast_only = absolutize_prefixes(manifest.ast_only);

    // Collect all .logos files under the module root and bucket them:
    //   exclude  → drop entirely (not in archive at all).
    //   ast_only → loaded for AST emission, skipped at codegen (host-only
    //     externs make the .o invalid for user-link-time use; the AST
    //     is still needed by the metacall JIT at compile time).
    //   regular  → both .o and .hermes0.
    auto all_files = collect_logos_files(root);
    std::vector<std::string> codegen_files;
    std::vector<std::string> ast_only_files;
    codegen_files.reserve(all_files.size());
    for (auto& f : all_files) {
        if (matches_any(f, abs_excludes)) continue;
        if (matches_any(f, abs_ast_only)) ast_only_files.push_back(f);
        else                              codegen_files.push_back(f);
    }
    if (codegen_files.empty() && ast_only_files.empty()) {
        std::fprintf(stderr, "emit_module: no .logos files found under %s\n",
                     root.c_str());
        return false;
    }
    if (verbose) {
        std::fprintf(stderr, "emit_module: found %zu codegen file(s), %zu ast-only file(s)\n",
                     codegen_files.size(), ast_only_files.size());
    }

    // Resolve `depends X` manifest entries to absolute archive paths and
    // prepend them to the loader's extra-lib list, so a module being built
    // sees its declared dependencies' .hermes0 packages before any user
    // -l files contribute. (Phase 1 of the three-layer stdlib split — see
    // docs/core-port/three-layer-split.md.)
    std::vector<std::string> all_lib_files;
    // The module's declared dependency archives (manifest `depends`), kept for
    // the import-table member — the libraries this module imports.
    std::vector<std::string> import_dep_archives;
    {
        std::string err;
        auto dep_archives = resolve_manifest_depends(manifest, search_paths, err);
        if (!err.empty()) {
            std::fprintf(stderr, "emit_module: %s\n", err.c_str());
            return false;
        }
        import_dep_archives = dep_archives;
        all_lib_files = std::move(dep_archives);
        for (auto& f : opts.extra_lib_files) all_lib_files.push_back(f);
        if (verbose && !manifest.depends.empty()) {
            std::fprintf(stderr, "emit_module: resolved %zu depends entry(ies)\n",
                         manifest.depends.size());
        }
    }

    // Parse all files. load_modules follows `use`-deps transitively from
    // each entry; we load both buckets, dedup by path. ast_only files
    // load LAST so transitive deps reachable from codegen files surface
    // in the codegen bucket first.
    std::vector<ParsedModule> modules;
    {
        std::unordered_set<std::string> seen;
        auto load_bucket = [&](const std::vector<std::string>& bucket) {
            for (auto& file : bucket) {
                auto mods = load_modules(file, search_paths, nullptr,
                                         all_lib_files, manifest.prelude,
                                         abs_excludes);
                for (auto& m : mods) {
                    if (seen.insert(m.path).second)
                        modules.push_back(std::move(m));
                }
            }
        };
        load_bucket(codegen_files);
        load_bucket(ast_only_files);
    }

    if (modules.empty()) {
        std::fprintf(stderr, "emit_module: failed to load any modules\n");
        return false;
    }
    if (verbose) {
        std::fprintf(stderr, "emit_module: loaded %zu module(s) total\n", modules.size());
    }

    auto is_ast_only_path = [&](const std::string& path) {
        return matches_any(path, abs_ast_only);
    };

    // Resolve absolute path of the per-file target if --only-file is set.
    // load_modules canonicalizes paths, so match against the same form.
    std::string only_file_canon;
    if (!opts.only_file.empty()) {
        std::error_code can_ec;
        only_file_canon = fs::weakly_canonical(opts.only_file, can_ec).string();
        if (can_ec) only_file_canon = opts.only_file;
    }

    // Output paths. In per-file mode, write `<output_path>.o` and
    // `<output_path>.hermes0` directly (no archive). In standard mode,
    // intermediate files go in a temp dir and `ar` builds the .a.
    std::string obj_path;
    std::string h0_path;
    if (!opts.only_file.empty()) {
        obj_path = output_path + ".o";
        h0_path  = output_path + ".hermes0";
    } else {
        auto tmp_dir = fs::temp_directory_path() / ("logos_emit_" + manifest.name);
        std::error_code ec;
        fs::create_directories(tmp_dir, ec);
        obj_path = (tmp_dir / (manifest.name + ".o")).string();
        h0_path  = (tmp_dir / (manifest.name + ".hermes0")).string();
    }

    // Build AST + filename arrays for codegen — skip ast_only modules.
    // .hermes0 takes everything (incl. ast_only).
    //
    // Compile-to-object also needs ast_only modules in its asts vector
    // — the metaprog dispatch loop (#21 closure) JIT-compiles their
    // handler fn bodies + reads their `#[metaprog_handler]` triggers.
    // We stamp ast_only with from_binary=true after dispatch so sema's
    // post-dispatch pass treats those items as already-emitted (no
    // codegen for host-extern-using fns).
    std::vector<hermes::Hermes> asts;
    std::vector<std::string> filenames;
    std::vector<bool>        ast_only_flags;       // parallel to asts
    std::vector<bool>        from_binary_module_flags;  // parallel to asts
    std::vector<ParsedModule> modules_for_h0;
    for (auto& m : modules) {
        modules_for_h0.push_back({m.path, m.package, m.ast});  // Hermes is copy-on-write safe
        bool ao = is_ast_only_path(m.path);
        filenames.push_back(m.path);
        ast_only_flags.push_back(ao);
        from_binary_module_flags.push_back(m.from_binary_module);
        asts.push_back(std::move(m.ast));
    }

    // Phase 6 lazy mode: skip compile_to_object entirely. The lazy archive
    // ships only the parsed AST; the consumer's sema lowers items locally
    // on use. No .o, no LIR blob, no exports trailer.
    size_t original_ast_count = asts.size();
    StdlibExports exports;
    std::vector<uint8_t> lir_blob;
    if (manifest.lazy) {
        if (verbose) {
            std::fprintf(stderr,
                "emit_module: lazy mode — skipping codegen for %zu file(s)\n",
                filenames.size());
        }
    } else {
        // Compile to object file (eager — the default).
        if (verbose) {
            std::fprintf(stderr, "emit_module: compiling %zu file(s)%s%s%s → %s\n",
                         filenames.size(),
                         only_file_canon.empty() ? "" : " (filtering to ",
                         only_file_canon.empty() ? "" : only_file_canon.c_str(),
                         only_file_canon.empty() ? "" : ")",
                         obj_path.c_str());
        }
        if (!compile_to_object(asts, filenames, ast_only_flags,
                               from_binary_module_flags, obj_path,
                               only_file_canon, &exports, &lir_blob,
                               /*module_name=*/manifest.name,
                               /*implicit_prelude=*/manifest.prelude,
                               /*dep_archives=*/all_lib_files)) {
            std::fprintf(stderr, "emit_module: compilation failed\n");
            return false;
        }
    }
    if (verbose) {
        std::fprintf(stderr,
            "emit_module: exports — %zu struct templates, %zu enum templates, %zu fn templates, %zu blanket impls, %zu concrete impls\n",
            exports.struct_templates.size(),
            exports.enum_templates.size(),
            exports.fn_templates.size(),
            exports.blanket_impls.size(),
            exports.concrete_impls.size());
        std::fprintf(stderr,
            "emit_module: LIR blob — %zu bytes\n", lir_blob.size());
    }
    // Harvest synth docs (appended by metaprog dispatch). These carry
    // derive-emitted items (e.g. cow.logos's `BranchNode`) that must
    // appear in the .hermes0 archive — otherwise downstream consumers
    // re-load the binary stdlib without dispatch firing and fail to
    // resolve those symbols.
    for (size_t i = original_ast_count; i < asts.size(); ++i) {
        std::string path = i < filenames.size() ? filenames[i] : std::string();
        // Skip metacall thunk asts — they're for JIT only, not for
        // downstream sema. Synth docs from item-blob substitution carry
        // the inherited package name; read it.
        if (path == "<metaprog>") continue;
        // Multiple synth docs share filename "<metaprog-blob-subst>" —
        // disambiguate so module_loader's visited_files dedup doesn't
        // drop all but the first when the .hermes0 is loaded by user.
        path += "#" + std::to_string(i - original_ast_count);
        std::string pkg;
        auto root_av = asts[i].root_object();
        if (root_av.is_pointer()) {
            auto root_map = root_av.as_tiny_map();
            if (root_map.has_key(logos::compiler::ast::NAME)) {
                auto nm = root_map.get(logos::compiler::ast::NAME.code);
                if (!nm.is_null() && nm.is_pointer()) {
                    pkg = std::string(hermes::StringView(
                        nm.to_offset(), asts[i].holder()).view());
                }
            }
            if (root_map.has_key(logos::compiler::ast::mod::PATH_PARTS)) {
                auto pp = root_map.get(logos::compiler::ast::mod::PATH_PARTS.code);
                if (!pp.is_null() && pp.is_pointer()) {
                    auto* arr = reinterpret_cast<const hermes::ObjectArray*>(
                        asts[i].holder()->base() + pp.to_offset().value());
                    for (uint64_t j = 0; j < arr->size(); ++j) {
                        auto part_av = arr->get(j, asts[i].holder()->base());
                        if (!part_av.is_pointer()) continue;
                        auto part_map = hermes::TinyMapView(
                            part_av.to_offset(), asts[i].holder());
                        if (part_map.has_key(logos::compiler::ast::NAME)) {
                            auto nm = part_map.get(logos::compiler::ast::NAME.code);
                            if (!nm.is_null() && nm.is_pointer()) {
                                pkg += ".";
                                pkg += std::string(hermes::StringView(
                                    nm.to_offset(), asts[i].holder()).view());
                            }
                        }
                    }
                }
            }
        }
        modules_for_h0.push_back({path, pkg, asts[i]});
    }

    // .hermes0: in per-file mode, contains only the target file's AST.
    if (!opts.only_file.empty()) {
        std::vector<ParsedModule> single;
        for (auto& m : modules_for_h0) {
            std::error_code can_ec;
            auto canon = fs::weakly_canonical(m.path, can_ec).string();
            if (can_ec) canon = m.path;
            if (canon == only_file_canon ||
                (canon.size() >= only_file_canon.size() &&
                 canon.compare(canon.size() - only_file_canon.size(),
                               only_file_canon.size(), only_file_canon) == 0)) {
                single.push_back(m);
                break;
            }
        }
        if (single.empty()) {
            std::fprintf(stderr,
                "emit_module: --only-file '%s' did not match any source file in the manifest\n",
                opts.only_file.c_str());
            return false;
        }
        if (verbose) {
            std::fprintf(stderr, "emit_module: writing → %s (single file)\n", h0_path.c_str());
        }
        if (!write_hermes0(h0_path, single, &exports, &lir_blob)) {
            std::fprintf(stderr, "emit_module: .hermes0 write failed\n");
            return false;
        }
        // Per-file mode also wraps .hermes0 → .hermes0.o so lforge can
        // archive it without ld.lld emitting an "is neither ET_REL nor
        // LLVM bitcode" warning at downstream link time.
        // Match emit_module-mode naming: "<base>.hm0" (≤15 chars in ar).
        std::string h0_obj =
            h0_path.substr(0, h0_path.find_last_of('.')) + ".hm0";
        {
            std::ostringstream cmd;
            cmd << "objcopy -I binary -O elf64-x86-64 "
                << "--rename-section .data=.lhermes "
                << h0_path << " " << h0_obj;
            if (verbose) {
                std::fprintf(stderr, "emit_module: %s\n", cmd.str().c_str());
            }
            if (std::system(cmd.str().c_str()) != 0) {
                std::fprintf(stderr, "emit_module: objcopy wrap failed\n");
                return false;
            }
        }
        if (verbose) {
            std::fprintf(stderr, "emit_module: wrote %s + %s\n",
                         obj_path.c_str(), h0_obj.c_str());
        }
        return true;
    }

    // A-AST: do not re-export the AST of dependency modules pulled in from a
    // lower-layer archive (from_binary_module=true). Those definitions are
    // owned by their canonical archive (lang.a / mem.a), which is always on a
    // consumer's search path in a layered link — build_binary_index routes
    // dep packages to that owner via its owned-only `.pkgi`, and the owner's
    // own prelude-sibling pull supplies the foundational traits. The embedded
    // copy here was pure dead weight (~2.6 MB of lang+mem AST on std.hm0) that
    // never won a lookup. Synth docs (the tail past from_binary_module_flags)
    // are owned by THIS module and are kept.
    //
    // The LIR blob still carries dep definitions for codegen self-containment;
    // thinning that is the separate A-LIR step (TypeUID indirection).
    std::vector<ParsedModule> own_modules_for_h0;
    own_modules_for_h0.reserve(modules_for_h0.size());
    size_t dropped_dep_asts = 0;
    for (size_t i = 0; i < modules_for_h0.size(); ++i) {
        if (i < from_binary_module_flags.size() && from_binary_module_flags[i]) {
            ++dropped_dep_asts;
            continue;
        }
        own_modules_for_h0.push_back(modules_for_h0[i]);
    }
    if (verbose) {
        std::fprintf(stderr,
            "emit_module: A-AST — keeping %zu own module(s), dropped %zu dep AST(s) from .hermes0\n",
            own_modules_for_h0.size(), dropped_dep_asts);
        std::fprintf(stderr, "emit_module: writing → %s\n", h0_path.c_str());
    }
    uint64_t mflags = manifest.lazy ? module_flag::LAZY : 0;
    if (!write_hermes0(h0_path, own_modules_for_h0,
                       manifest.lazy ? nullptr : &exports,
                       manifest.lazy ? nullptr : &lir_blob,
                       mflags)) {
        std::fprintf(stderr, "emit_module: .hermes0 write failed\n");
        return false;
    }

    // Wrap .hermes0 as a relocatable ELF object so ld.lld doesn't warn
    // about a non-ET_REL archive member when downstream binaries link
    // against this archive. The data lives in a non-ALLOC `.lhermes`
    // section; module_loader looks for that section by name.
    // Wrap into "<basename>.hm0" (must stay <=15 chars including the
    // trailing `/` separator that ar appends, otherwise the name spills
    // into the GNU extended name table — which our ar parser doesn't
    // chase).
    std::string h0_obj_path =
        h0_path.substr(0, h0_path.find_last_of('.')) + ".hm0";
    {
        std::ostringstream cmd;
        cmd << "objcopy -I binary -O elf64-x86-64 "
            << "--rename-section .data=.lhermes "
            << h0_path << " " << h0_obj_path;
        if (verbose) {
            std::fprintf(stderr, "emit_module: %s\n", cmd.str().c_str());
        }
        if (std::system(cmd.str().c_str()) != 0) {
            std::fprintf(stderr, "emit_module: objcopy wrap failed\n");
            return false;
        }
    }

    // Embedded package-name index. The loader's build_binary_index needs
    // to know which packages each archive provides; reading the full .hm0
    // is ~30-40MB of memcpy per archive (filesystem cache is warm, but
    // alloc + zero-init + copy still costs ~40ms across 4-archive layer
    // builds). The .pkgi member ships the package list as ASCII (one
    // package per line) wrapped in an ELF .lpkgindex non-ALLOC section
    // (mirrors the .hm0 → .lhermes wrap so ld.lld doesn't warn about
    // non-ET_REL archive members). The streaming AR reader pulls it out
    // without touching .hm0 bytes. Member name must stay <=15 chars.
    std::string pkgi_raw_path =
        h0_path.substr(0, h0_path.find_last_of('.')) + ".pkgi.raw";
    {
        std::ofstream f(pkgi_raw_path);
        if (!f) {
            std::fprintf(stderr, "emit_module: cannot write pkgi raw '%s'\n",
                         pkgi_raw_path.c_str());
            return false;
        }
        // Advertise only packages this archive OWNS — the modules from the
        // manifest's own sources (+ synth docs), NOT the dependency modules it
        // merely embeds for self-containment. modules_for_h0's first
        // from_binary_module_flags.size() entries are the loaded modules (in
        // order; from_binary_module=true means pulled from a lower-layer
        // archive); the tail are owned synth docs.
        //
        // Why this matters: build_binary_index (module_loader) maps
        // package→archive first-wins by filesystem iteration order. If a layer
        // archive advertised the dependency packages it embeds (e.g. mem.a
        // listing logos.lang.option), a STALE such archive sitting in the -L
        // dir could shadow the fresh canonical owner (lang.a) during an
        // incremental rebuild — silently embedding stale dependency bytes.
        // Advertising owned-only makes the canonical owner the sole index
        // entry, so resolution always finds the fresh archive. (The embedded
        // copy in .hm0 stays for sema self-containment but is never selected
        // over the owner.)
        std::set<std::string> seen;
        for (size_t i = 0; i < modules_for_h0.size(); ++i) {
            auto& m = modules_for_h0[i];
            if (m.package.empty()) continue;
            // Skip dependency modules embedded from a lower-layer archive.
            if (i < from_binary_module_flags.size() && from_binary_module_flags[i])
                continue;
            if (seen.insert(m.package).second) f << m.package << "\n";
        }
    }
    std::string pkgi_obj_path =
        h0_path.substr(0, h0_path.find_last_of('.')) + ".pkgi";
    {
        std::ostringstream cmd;
        cmd << "objcopy -I binary -O elf64-x86-64 "
            << "--rename-section .data=.lpkgindex "
            << pkgi_raw_path << " " << pkgi_obj_path;
        if (verbose) {
            std::fprintf(stderr, "emit_module: %s\n", cmd.str().c_str());
        }
        if (std::system(cmd.str().c_str()) != 0) {
            std::fprintf(stderr, "emit_module: objcopy pkgi wrap failed\n");
            return false;
        }
    }

    // Import-table member: a standalone Hermes doc listing the libraries this
    // module imports (its `depends`), one (file_name, doc_name) per local
    // arena_id. Shipped as its own `.imp` member (wrapped in a `.limports`
    // ELF section, mirroring the .hm0/.pkgi wrap) so a tool can read just this
    // small member for fast dependency inspection, and so a cross-arena
    // ExternalRef's arena_id resolves through it. doc_name is "" today (one
    // document per .hermes0; multi-doc reserved).
    std::string imp_obj_path;
    {
        std::vector<hermes::ImportEntry> imports;
        imports.reserve(import_dep_archives.size());
        for (const auto& a : import_dep_archives) {
            imports.push_back({fs::path(a).filename().string(), std::string()});
        }
        auto blob = hermes::build_import_table_blob(manifest.name, imports);
        if (!blob) {
            std::fprintf(stderr, "emit_module: import-table build failed\n");
            return false;
        }
        std::string imp_raw_path =
            h0_path.substr(0, h0_path.find_last_of('.')) + ".imp.raw";
        {
            std::ofstream f(imp_raw_path, std::ios::binary);
            if (!f) {
                std::fprintf(stderr, "emit_module: cannot write imp raw '%s'\n",
                             imp_raw_path.c_str());
                return false;
            }
            f.write(reinterpret_cast<const char*>(blob->data()),
                    static_cast<std::streamsize>(blob->size()));
        }
        imp_obj_path =
            h0_path.substr(0, h0_path.find_last_of('.')) + ".imp";
        std::ostringstream cmd;
        cmd << "objcopy -I binary -O elf64-x86-64 "
            << "--rename-section .data=.limports "
            << imp_raw_path << " " << imp_obj_path;
        if (verbose) {
            std::fprintf(stderr, "emit_module: %s\n", cmd.str().c_str());
        }
        if (std::system(cmd.str().c_str()) != 0) {
            std::fprintf(stderr, "emit_module: objcopy imp wrap failed\n");
            return false;
        }
    }

    // Create .a archive: lazy mode has no .o (codegen skipped), pack just
    // the .hm0 wrapper + pkgi index + import table. Eager mode also packs NAME.o.
    {
        std::ostringstream cmd;
        cmd << "ar rcs " << output_path;
        if (!manifest.lazy) cmd << " " << obj_path;
        cmd << " " << h0_obj_path << " " << pkgi_obj_path << " " << imp_obj_path;
        if (verbose) {
            std::fprintf(stderr, "emit_module: %s\n", cmd.str().c_str());
        }
        if (std::system(cmd.str().c_str()) != 0) {
            std::fprintf(stderr, "emit_module: ar failed\n");
            return false;
        }
    }

    if (verbose) {
        std::fprintf(stderr, "emit_module: wrote %s\n", output_path.c_str());
    }
    return true;
}

} // namespace logos::compiler
