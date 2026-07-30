// Logos project — https://github.com/victor-smirnov/logos
//
// logosc — Logos compiler driver (iteration 1).
//
// Pipeline: .logos file → PEG parser → Writ AST → MLIR → LLVM IR → .o file.

#include "emit_module.hpp"
#include "metaprog_dispatch.hpp"
#include "mlir_gen.hpp"
#include "compile_pipeline.hpp"
#include "llvm_compat.hpp"
#include "module_manifest.hpp"
#include <chrono>
#include <map>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
#include "module_loader.hpp"
#include <logos/compiler/borrow_check.hpp>
#include <logos/compiler/sema.hpp>
#include <logos/compiler/ast.hpp>
#include <logos/compiler/lir.hpp>
#include <logos/compiler/lir_mirror.hpp>  // lir_mirror_macro_arg_get (metacall shim)
#include <logos/compiler/rule_ir.hpp>    // logos_rule_ir (compiler-parsed rule IR handoff)
#include <logos/compiler/mono.hpp>
#include <logos/writ/compat.hpp>

#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/compiler/ast.hpp>

// MLIR
#include <mlir/IR/MLIRContext.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/Dialect/ControlFlow/IR/ControlFlowOps.h>
#include <mlir/Dialect/LLVMIR/LLVMDialect.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Pass/PassManager.h>
#include <mlir/Conversion/FuncToLLVM/ConvertFuncToLLVMPass.h>
#include <mlir/Conversion/ArithToLLVM/ArithToLLVM.h>
#include <mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h>
#include <mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h>
#include <mlir/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.h>
#include <mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h>
#include <mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h>
#include <mlir/Target/LLVMIR/Export.h>

// LLVM
#include <llvm/IR/Module.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
// LLVM new pass manager (optimization pipeline)
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Transforms/IPO/Internalize.h>
#include <llvm/Transforms/IPO/GlobalDCE.h>
#include <llvm/IR/PassManager.h>

// JIT (Phase 4)
#include <logos/jit/jit.hpp>

// Generated parser (for runtime parse of metaprog-emitted source).
#include "logos_parser.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Phase 7 slice 1+2: AST-emit seam for metaprog hooks.
//
// `logos_emit_source(src)` parses a chunk of Logos text via the
// in-process parser and appends the resulting AST as a fresh entry
// in `asts`. Sema's next iteration sees it as if it were another
// source file. Host-side dedup by source-text means a naive
// always-emit hook still converges in 2 iters: iter 0 parses +
// appends + flags any_emitted; iter 1 hits the existing entry and
// the loop breaks.
//
// Splicing whole documents (vs in-place mutation of asts[0]) reuses
// the parser, sidesteps cross-arena pointer concerns, and stays
// trivially compactable via per-document writ::clone().
namespace {
std::set<std::string>*               g_emit_seen   = nullptr;
bool*                                g_any_emitted = nullptr;
std::vector<logos::writ::Writ>*  g_asts        = nullptr;
std::vector<std::string>*            g_filenames   = nullptr;
std::vector<bool>*                   g_from_binary = nullptr;
// Module system: per-AST owning-module id, parallel to g_from_binary. New
// asts appended by metaprog hooks belong to the module being compiled, so
// they're stamped with g_self_module_id (empty for a plain user program).
std::vector<std::string>*            g_module_ids  = nullptr;
std::string                          g_self_module_id;
size_t                               g_user_root_idx = 0;
// Phase 7 diagnostics: hooks call logos_metaprog_error(msg) to push
// a structured error. The driver drains this after each hook and
// fails compilation if non-empty. Spans/locations TBD when hook
// arg-passing lands (an emitted node ref will carry source info).
std::vector<std::string>*            g_metaprog_diags = nullptr;
const char*                          g_current_hook_name = nullptr;
// Function-style macros (slice 1.3b): the metacall driver-invoke loop
// points this at the active LProgram::macro_arg_blobs so the host shim
// `logos_macro_arg(site_id, arg_idx)` can resolve each thunk's args.
// Each blob is laid out as `[u64 size][bytes]`; the shim returns
// `&blob[8]` (past the size prefix) to match the ExprBlob/WritStatic ABI.
const logos::compiler::lir_view::ObjectMapRef* g_macro_args = nullptr;

// `--dump-metaprog` provenance tracking. When the metaprog driver is
// about to invoke a hook or metacall thunk, it sets g_current_emit_ctx
// (file:line of the trigger / metacall + callee name + iteration). Any
// document appended to g_asts during that invocation inherits the
// context, recorded by index into g_ast_provenance (sparse — entries
// stay nullopt for non-metaprog-emitted docs).
//
// Provenance is doc-level rather than item-level because the splice
// path (logos_emit_item_blob / _subst, logos_emit_source) appends a
// whole new Writ document per emission; per-item stamping would
// require threading the context deeper into sema, which gives no
// extra information for the dump UX (file-level granularity is what
// users select on).
// EmitProvenance struct is exposed in metaprog_dispatch.hpp (shared
// between main.cpp and emit_module.cpp). Locally we just keep the
// runtime state.
using logos::compiler::EmitProvenance;
EmitProvenance                       g_current_emit_ctx;
bool                                 g_current_emit_ctx_valid = false;
std::vector<std::optional<EmitProvenance>>* g_ast_provenance = nullptr;

// Record provenance for the most recently appended ast (call AFTER
// g_asts->push_back). No-op if the dump driver hasn't allocated a
// provenance vector or the context is invalid.
void record_emit_provenance() {
    if (!g_ast_provenance || !g_current_emit_ctx_valid || !g_asts) return;
    while (g_ast_provenance->size() < g_asts->size())
        g_ast_provenance->emplace_back(std::nullopt);
    g_ast_provenance->back() = g_current_emit_ctx;
}

// ── --gen-dir: generated sources as REAL files (human + debugger) ─────────
//
// Every quote-emitted synth module is rendered to
// `<gen-dir>/<pkg>.<seq>[.<target>].gen.logos`, then the rendered text is
// REPARSED and the reparse REPLACES the in-memory synth doc — the chunk's AST
// is literally the parse of the dump file, so sema diagnostics and DWARF
// line info agree with what a human (or gdb/lldb) reads BY CONSTRUCTION.
// A renderer fidelity gap surfaces here as a loud parse/shape failure and
// falls back to the in-memory doc + the "<metaprog-blob-subst>" pseudo-name.
//
// The `.gen.logos` suffix is a CONTRACT: sema's metaprog-discovery body-stub
// gate (sema_decl.cpp is_synth_blob) and the --expand readback recognise
// synth chunks by it once the pseudo-name is replaced.
std::string g_gen_dir;   // empty = disabled
bool        g_gen_debug_info = false;   // -g: only then is the reparse-SWAP
                                        // performed (aligning DWARF with the
                                        // dump); without -g dumps are
                                        // display-only and the in-memory doc
                                        // stays authoritative.
int         g_gen_seq = 0;

// Top-level item-kind census of a module doc (junk-filtered) — the shape
// gate for the render→reparse round-trip.
void gen_item_census(logos::writ::Writ& doc, std::map<int32_t, int>& out) {
    namespace la = logos::compiler::ast;
    using logos::writ::WritAccess;
    using logos::writ::arena_offset_t;
    auto root_off = WritAccess::root_offset(doc);
    auto root = logos::writ::TinyMapView(arena_offset_t(root_off.value()),
                                         doc.holder());
    if (!root.has_key(la::ITEMS.code)) return;
    auto iav = root.get(la::ITEMS.code);
    if (iav.is_null() || !iav.is_pointer()) return;
    auto arr = logos::writ::as_array(iav, doc.holder());
    for (uint64_t i = 0; i < arr.size(); ++i) {
        auto e = arr.get(i);
        if (e.is_null() || !e.is_pointer()) continue;
        if (logos::writ::TypeTag::read_before(e.resolve()).type_code()
                != logos::writ::type_hash::TinyObjectMap) continue;
        auto t = logos::writ::as_tinymap(e, doc.holder());
        if (!t.has_key(la::CODE.code)) continue;
        auto cav = t.get(la::CODE.code);
        if (cav.is_null() || !cav.is_value()) continue;
        int32_t c = cav.as_value<int32_t>();
        if (c == la::FN.code || c == la::EXTERN_FN.code || c == la::STRUCT.code
            || c == la::ENUM.code || c == la::TRAIT_DEF.code
            || c == la::IMPL_BLOCK.code || c == la::CONST_DEF.code
            || c == la::STATIC_DEF.code || c == la::TYPE_ALIAS.code)
            out[c]++;
    }
}

// Renders + dumps + reparses `doc`. On success returns {reparsed doc, path};
// on any failure warns and returns nullopt (caller keeps the original doc).
std::optional<std::pair<logos::writ::Writ, std::string>>
try_gen_dump(logos::writ::Writ& doc) {
    namespace fs = std::filesystem;
    namespace la = logos::compiler::ast;
    using logos::writ::WritAccess;
    using logos::writ::arena_offset_t;

    auto root_off = WritAccess::root_offset(doc);
    std::string body = logos::compiler::render_module_source_for_dump(
        doc.holder(), root_off);
    if (body.empty()) {
        std::fprintf(stderr, "logosc: --gen-dir: render produced no text; "
                             "keeping in-memory synth doc\n");
        return std::nullopt;
    }

    // Provenance header (plain `//` comments — lexer trivia).
    std::string text = "// GENERATED by metaprog — DO NOT EDIT. (--gen-dir dump.)\n";
    if (g_gen_debug_info)
        text +=
            "// Compiled WITH -g: the compiler reparses THIS file as the "
            "emitted module,\n"
            "// so the line numbers here ARE the debug-info lines.\n";
    if (g_current_emit_ctx_valid) {
        const auto& c = g_current_emit_ctx;
        text += "// emitted by: " + c.callee_name;
        if (!c.trigger.empty())
            text += "  (trigger '" + c.trigger + "' on '" + c.target_name + "')";
        text += "\n";
        if (!c.src_file.empty())
            text += "// site:       " + c.src_file + ":"
                  + std::to_string(c.src_line) + "\n";
    }
    text += "\n";
    text += body;
    if (text.back() != '\n') text.push_back('\n');

    // File name: <pkg>.<seq>[.<target>].gen.logos
    std::string pkg;
    {
        auto root = logos::writ::TinyMapView(arena_offset_t(root_off.value()),
                                             doc.holder());
        if (root.has_key(la::NAME.code)) {
            auto nm = root.get(la::NAME.code);
            if (!nm.is_null() && nm.is_pointer())
                pkg = std::string(logos::writ::StringView(nm, doc.holder()).view());
        }
        if (root.has_key(la::mod::PATH_PARTS.code)) {
            auto pp = root.get(la::mod::PATH_PARTS.code);
            if (!pp.is_null() && pp.is_pointer()) {
                auto arr = logos::writ::as_array(pp, doc.holder());
                for (uint64_t i = 0; i < arr.size(); ++i) {
                    auto e = arr.get(i);
                    if (e.is_null() || !e.is_pointer()) continue;
                    auto t = logos::writ::as_tinymap(e, doc.holder());
                    if (t.is_null() || !t.has_key(la::NAME.code)) continue;
                    auto sn = t.get(la::NAME.code);
                    if (sn.is_null() || !sn.is_pointer()) continue;
                    pkg += ".";
                    pkg += std::string(
                        logos::writ::StringView(sn, doc.holder()).view());
                }
            }
        }
    }
    if (pkg.empty()) pkg = "anon";
    auto sanitize = [](std::string s) {
        for (auto& ch : s)
            if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '.'
                  || ch == '_')) ch = '_';
        return s;
    };
    // Name: <pkg>[.<trigger-file-stem>].<seq>[.<target>].gen.logos — the
    // trigger file discriminates same-package modules from different compiles
    // sharing one gen dir (e.g. every lforge test is `package test`).
    std::string stem = sanitize(pkg);
    if (g_current_emit_ctx_valid && !g_current_emit_ctx.src_file.empty()
        && g_current_emit_ctx.src_file[0] != '<') {
        std::string base = fs::path(g_current_emit_ctx.src_file).stem().string();
        if (!base.empty() && base != pkg) stem += "." + sanitize(base);
    } else {
        // Pseudo-site trigger (a harvested decl chunk, src_file "<metaprog>"):
        // the file stem can't discriminate, and the per-process seq collides
        // across compiles sharing one gen dir. Use the first item's NAME
        // (e.g. Gen$G1$u64Leaf) — unique per generated family blob set.
        auto root = logos::writ::TinyMapView(arena_offset_t(root_off.value()),
                                             doc.holder());
        if (root.has_key(la::ITEMS.code)) {
            auto iav = root.get(la::ITEMS.code);
            if (!iav.is_null() && iav.is_pointer()) {
                auto arr = logos::writ::as_array(iav, doc.holder());
                for (uint64_t i = 0; i < arr.size(); ++i) {
                    auto e = arr.get(i);
                    if (e.is_null() || !e.is_pointer()) continue;
                    auto t = logos::writ::as_tinymap(e, doc.holder());
                    if (t.is_null()) continue;
                    // Item NAME; an inherent impl has none — use its
                    // receiver type's name instead.
                    auto read_nm = [&](logos::writ::TinyMapView m) {
                        std::string r;
                        if (!m.is_null() && m.has_key(la::NAME.code)) {
                            auto nv = m.get(la::NAME.code);
                            if (!nv.is_null() && nv.is_pointer())
                                r = std::string(logos::writ::StringView(
                                        nv, doc.holder()).view());
                        }
                        return r;
                    };
                    std::string nm = read_nm(t);
                    if (nm.empty() && t.has_key(la::TYPE.code)) {
                        auto tv2 = t.get(la::TYPE.code);
                        if (!tv2.is_null() && tv2.is_pointer())
                            nm = read_nm(logos::writ::as_tinymap(
                                     tv2, doc.holder()));
                    }
                    if (!nm.empty()) { stem += "." + sanitize(nm); break; }
                }
            }
        }
    }
    // ADR 0021 C3: a metaclass FACTORY family (ADR 0021 §3) is emitted as many
    // separate blobs (leaf/branch/tree/CoW-handle structs + BtLeaf/BtBranch/Ctr/
    // CtrFamily impls); the stem above picks each blob's FIRST item name, which
    // for an impl is its TRAIT ("BtBranch", "Ctr", …) — colliding across every
    // family. Every family member's name carries the `@hs` tag `Hs<016x>`
    // (member struct names, impl receiver types). Scan the items for that tag and
    // fold it into the stem so families are distinguishable at scale. (Non-family
    // dumps carry no such token → no change.)
    {
        auto extract_family_tag = [](std::string_view s) -> std::string {
            // `Hs` + 16 lowercase-hex — the identifier-safe @hs family spelling.
            for (size_t i = 0; i + 18 <= s.size(); ++i) {
                if (s[i] != 'H' || s[i + 1] != 's') continue;
                bool ok = true;
                for (int j = 0; j < 16; ++j)
                    if (!std::isxdigit(static_cast<unsigned char>(s[i + 2 + j]))) {
                        ok = false; break;
                    }
                if (ok) return std::string(s.substr(i, 18));
            }
            return "";
        };
        std::string fam_tag;
        auto root = logos::writ::TinyMapView(arena_offset_t(root_off.value()),
                                             doc.holder());
        if (root.has_key(la::ITEMS.code)) {
            auto iav = root.get(la::ITEMS.code);
            if (!iav.is_null() && iav.is_pointer()) {
                auto arr = logos::writ::as_array(iav, doc.holder());
                auto name_of = [&](logos::writ::TinyMapView m) -> std::string {
                    if (m.is_null() || !m.has_key(la::NAME.code)) return "";
                    auto nv = m.get(la::NAME.code);
                    if (nv.is_null() || !nv.is_pointer()) return "";
                    return std::string(
                        logos::writ::StringView(nv, doc.holder()).view());
                };
                for (uint64_t i = 0; i < arr.size() && fam_tag.empty(); ++i) {
                    auto e = arr.get(i);
                    if (e.is_null() || !e.is_pointer()) continue;
                    auto t = logos::writ::as_tinymap(e, doc.holder());
                    if (t.is_null()) continue;
                    fam_tag = extract_family_tag(name_of(t));   // struct/fn NAME
                    if (fam_tag.empty() && t.has_key(la::TYPE.code)) {
                        auto tv = t.get(la::TYPE.code);          // impl receiver
                        if (!tv.is_null() && tv.is_pointer())
                            fam_tag = extract_family_tag(
                                name_of(logos::writ::as_tinymap(tv, doc.holder())));
                    }
                }
            }
        }
        if (!fam_tag.empty() && stem.find(fam_tag) == std::string::npos)
            stem += "." + fam_tag;
    }
    stem += "." + std::to_string(++g_gen_seq);
    if (g_current_emit_ctx_valid && !g_current_emit_ctx.target_name.empty())
        stem += "." + sanitize(g_current_emit_ctx.target_name);
    std::error_code ec;
    fs::create_directories(g_gen_dir, ec);
    fs::path out_path = fs::absolute(fs::path(g_gen_dir) / (stem + ".gen.logos"));
    {
        std::ofstream f(out_path);
        if (!f) {
            std::fprintf(stderr, "logosc: --gen-dir: cannot write '%s'\n",
                         out_path.c_str());
            return std::nullopt;
        }
        f << text;
    }

    // Without -g the dump is display-only: no DWARF will reference it, so
    // the reparse-swap buys nothing and the in-memory doc stays authoritative
    // (user request: swap only when debug info is asked for).
    if (!g_gen_debug_info) return std::nullopt;

    // Reparse the dump; the round-trip is the fidelity gate.
    logos::compiler::LogosParser parser(text);
    logos::writ::Writ ndoc = parser.parse_module();
    if (ndoc.is_null() || !parser.at_eof()) {
        std::fprintf(stderr,
            "logosc: --gen-dir: '%s' does not reparse (renderer fidelity gap) "
            "— keeping in-memory synth doc\n", out_path.c_str());
        return std::nullopt;
    }
    std::map<int32_t, int> a, b;
    gen_item_census(doc, a);
    gen_item_census(ndoc, b);
    if (a != b) {
        std::fprintf(stderr,
            "logosc: --gen-dir: '%s' reparse changes the item census "
            "(renderer fidelity gap) — keeping in-memory synth doc\n",
            out_path.c_str());
        return std::nullopt;
    }
    return std::make_pair(std::move(ndoc), out_path.string());
}
}

// Phase 7 slice 3a: read-only AST view for metaprog hooks.
//
// Returns base+size of the user's entry-file document into the
// caller's out-params. The module loader walks dependencies in
// post-order, so the entry file is at index `g_user_root_idx`
// (recorded right after load_modules, before any metaprog splice).
// Logos-side wraps the pair in `WritView<'a>` — non-owning
// fat-borrow whose 'a is the hook frame's lifetime. OView
// (RC-owning view) is the eventual API; the borrow is sound
// because the root doc outlives every hook invocation.
extern "C" void logos_get_module_ast(const uint8_t** out_base,
                                     uint64_t*       out_size) {
    if (!g_asts || g_asts->size() <= g_user_root_idx
        || !out_base || !out_size) {
        if (out_base) *out_base = nullptr;
        if (out_size) *out_size = 0;
        return;
    }
    auto* h = (*g_asts)[g_user_root_idx].holder();
    *out_base = h->base();
    *out_size = static_cast<uint64_t>(h->arena().head().used);
}

// Phase 7 slice 7: owning-view bindings.
//
// `logos_get_module_ast_oview` is the OView ctor's host-side: it fills
// (holder, base, size) from the user's entry-file document and bumps
// the holder's refcount by one. The Logos OView takes ownership of
// that ref and releases it via Drop -> logos_holder_release.
//
// The C++ MemHolder layout (atomic ref + Arena) does not match the
// stdlib `std.writ.mem_holder.MemHolder` POD, so OView treats the
// holder as opaque (`*mut u8`) and only ever passes it back to host
// fns — base/size are cached in OView itself at ctor time.
extern "C" void logos_get_module_ast_oview(void**           out_holder,
                                           const uint8_t**  out_base,
                                           uint64_t*        out_size) {
    if (!g_asts || g_asts->size() <= g_user_root_idx
        || !out_holder || !out_base || !out_size) {
        if (out_holder) *out_holder = nullptr;
        if (out_base)   *out_base   = nullptr;
        if (out_size)   *out_size   = 0;
        return;
    }
    auto* h = (*g_asts)[g_user_root_idx].holder();
    h->ref();
    *out_holder = h;
    *out_base   = h->base();
    *out_size   = static_cast<uint64_t>(h->arena().head().used);
}

extern "C" void logos_holder_release(void* h) {
    if (!h) return;
    static_cast<logos::writ::MemHolder*>(h)->unref();
}

// Phase 7 diagnostics: structured error from a metaprog hook.
// Copies the string (caller's buffer is hook-frame-scoped) and
// records the originating hook for the driver's report. No-op
// outside a hook frame (defensive — host accidents shouldn't crash).
extern "C" void logos_metaprog_error(const char* msg) {
    if (!g_metaprog_diags || !msg) return;
    std::string out;
    // A metacall/token-macro thunk is running: the driver computed the site's
    // file + SRC_LINE into g_current_emit_ctx before invoking it. Prefixing
    // here gives EVERY handler/walker `error()` a real location — the deem
    // pipeline's ~50 diagnostics acquire spans without touching one of them.
    if (g_current_emit_ctx_valid && !g_current_emit_ctx.src_file.empty()) {
        out.append(g_current_emit_ctx.src_file);
        out.append(":");
        out.append(std::to_string(g_current_emit_ctx.src_line));
        out.append(": ");
    }
    if (g_current_hook_name) {
        out.append("metaprog hook '");
        out.append(g_current_hook_name);
        out.append("': ");
    }
    out.append(msg);
    g_metaprog_diags->push_back(std::move(out));
}

// Phase 7 slice 13: error with span. `target_offset` is an offset
// into the user-root Writ doc; host reads SRC_LINE from the node
// there and prefixes the diag with `<file>:<line>:`. offset==0 (no
// span) falls back to the un-spanned form.
// Like logos_metaprog_error, but WITHOUT the site prefix: the caller already
// located the message itself, more precisely than the site can. The site's
// file:line is a fallback for handlers that do not know where they are; a
// reporter that DOES know must win, or every located diagnostic reads its
// position twice (ADR 0024 S0 — Deem locates against the query's own source).
extern "C" void logos_metaprog_error_located(const char* msg) {
    if (!g_metaprog_diags || !msg) return;
    std::string out;
    if (g_current_hook_name) {
        out.append("metaprog hook '");
        out.append(g_current_hook_name);
        out.append("': ");
    }
    out.append(msg);
    g_metaprog_diags->push_back(std::move(out));
}

extern "C" void logos_metaprog_error_at(uint32_t target_offset,
                                         const char* msg) {
    if (!g_metaprog_diags || !msg) return;
    std::string out;
    if (target_offset != 0
        && g_asts && g_asts->size() > g_user_root_idx
        && g_filenames && g_filenames->size() > g_user_root_idx) {
        auto tom = logos::writ::TinyMapView(
            logos::writ::arena_offset_t(target_offset),
            (*g_asts)[g_user_root_idx].holder());
        // SRC_LINE = key 24, u32 inline.
        auto line_av = tom.get(24);
        uint32_t line = (!line_av.is_null() && line_av.is_value())
                        ? line_av.as_value<uint32_t>() : 0;
        out.append((*g_filenames)[g_user_root_idx]);
        out.append(":");
        out.append(std::to_string(line));
        out.append(": ");
    }
    if (g_current_hook_name) {
        out.append("metaprog hook '");
        out.append(g_current_hook_name);
        out.append("': ");
    }
    out.append(msg);
    g_metaprog_diags->push_back(std::move(out));
}

// Shared emit_source core. The FILENAME is the channel tag: "<metaprog>" =
// user-facing generated items (harvested into module .wr0 archives so
// consumers resolve them); "<metaprog-thunk>" = metacall/item thunk sources
// (JIT-only, never archived — their use-lists carry thunk-frame package
// aliases like `std.lang.text` that a consumer's module loader cannot
// resolve).
static int32_t emit_source_tagged(const char* src, const char* filename) {
    if (!g_emit_seen || !g_any_emitted || !g_asts || !g_filenames
        || !g_from_binary || !src) return 0;
    std::string s(src);
    if (!g_emit_seen->insert(s).second) return 0;  // already emitted
    if (::getenv("LOGOS_TRACE_EMIT"))
        std::fprintf(stderr, "[emit_source]---\n%s\n---[/emit_source]\n", s.c_str());
    // --gen-dir: the emit_source channel is ALREADY source text — dump it
    // verbatim for human inspection. The chunk KEEPS its "<metaprog>"
    // pseudo-name: renaming would flip sema's is_synth_blob gate (these
    // chunks must JIT-compile during discovery, unlike blob-subst modules).
    // This channel is legacy (quotes replace it); dump-only is deliberate.
    if (!g_gen_dir.empty() && std::string_view(filename) == "<metaprog>") {
        namespace fs = std::filesystem;
        std::string text =
            "// GENERATED by metaprog (emit_source) — DO NOT EDIT. "
            "(--gen-dir dump; display-only,\n"
            "// this channel keeps its in-memory identity.)\n";
        if (g_current_emit_ctx_valid) {
            const auto& c = g_current_emit_ctx;
            text += "// emitted by: " + c.callee_name;
            if (!c.trigger.empty())
                text += "  (trigger '" + c.trigger + "' on '"
                      + c.target_name + "')";
            text += "\n";
            if (!c.src_file.empty())
                text += "// site:       " + c.src_file + ":"
                      + std::to_string(c.src_line) + "\n";
        }
        text += "\n";
        text += s;
        std::error_code ec;
        fs::create_directories(g_gen_dir, ec);
        fs::path p = fs::absolute(
            fs::path(g_gen_dir)
            / ("emit." + std::to_string(++g_gen_seq) + ".gen.logos"));
        std::ofstream f(p);
        if (f) f << text;
    }
    logos::compiler::LogosParser parser(s);
    auto ast = parser.parse_module();
    if (ast.is_null() || !parser.at_eof()) {
        std::fprintf(stderr, "logos_emit_source: parse failed near line %u\n",
                     parser.next_line());
        g_emit_seen->erase(s);  // allow caller to retry with corrected text
        return 0;
    }
    g_asts->push_back(std::move(ast));
    g_filenames->emplace_back(filename);
    g_from_binary->push_back(false);
    if (g_module_ids) g_module_ids->push_back(g_self_module_id);
    *g_any_emitted = true;
    record_emit_provenance();
    return 1;
}

extern "C" int32_t logos_emit_source(const char* src) {
    return emit_source_tagged(src, "<metaprog>");
}

// (plan, family) pairs already instantiated. One compile per process, and the
// two drain sites live in different functions — the driver's post-mono loop and
// the dispatch's own early drain — so the set is shared here rather than
// threaded twice.
static std::set<std::string> g_deem_plan_seen;
// CFG hashes already drained, shared by every drain site.
static std::unordered_set<uint64_t> g_early_drained_hashes;

// Escape a string for a Logos string literal (the table render_ctfe_lit's
// str case uses). Shared by every chunk renderer that splices text through a
// metacall argument.
static std::string esc_lit(std::string_view in) {
    std::string out;
    out.reserve(in.size() + 16);
    for (char c : in) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        case '\0': out += "\\0";  break;
        default:   out += c;      break;
        }
    }
    return out;
}

// Render + emit one `metacall __container_factory(hash, cfg_src)` chunk per
// undrained factory demand. Factored out of the driver's post-mono drain loop
// so the SAME rendering serves every point a demand can be discovered — the
// producer is called where the demand appears, not at one privileged phase.
// Returns the number of chunks emitted; `drained` deduplicates by CFG hash.
static int render_factory_chunks(const logos::compiler::lir::LProgram& prog,
                                 std::unordered_set<uint64_t>& drained) {
    bool factory_available = false;
    for (const auto& f : prog.functions)
        if (f && logos::compiler::bare_fn_name(f.name()) == "__container_factory")
            { factory_available = true; break; }
    if (!factory_available) return 0;
    int emitted = 0;
    for (const auto& fd : prog.factory_demands) {
        if (drained.count(fd.cfg_hash)) continue;
        auto src_it = prog.wstatic_sources.find(fd.cfg_hash);
        if (src_it == prog.wstatic_sources.end()) continue;
        char hex[17];
        std::snprintf(hex, sizeof hex, "%016llx", (unsigned long long)fd.cfg_hash);
        std::string esc = esc_lit(src_it->second);
        std::string chunk;
        chunk += "package logos.gen;\n";
        chunk += "use logos.lcm.canon.container_item;\n";
        chunk += "use logos.mem.bt.map;\n";
        chunk += "metacall __container_factory(\"";
        chunk += hex;
        chunk += "\", \"";
        chunk += esc;
        chunk += "\");\n";
        if (logos_emit_source(chunk.c_str())) ++emitted;
        drained.insert(fd.cfg_hash);
    }
    return emitted;
}

// ── a deem PLAN meets a generated family ────────────────────────────────────
// A `deem` whose source binds a container CLASS (`map: Map<K, V>`) is recorded
// as a plan rather than compiled at its declaration: which code it becomes is a
// question about the class TOGETHER WITH its type arguments, and that pair only
// exists where the factory generates a family. Here is that place.
//
// The plan is INSTANTIATED, not interpreted: the class parameter is re-spelled
// to the handle the factory just generated and the item is handed to the
// ORDINARY deem pipeline. One query name yields one overload per family, so the
// call site keeps writing the name it wrote — the generated type never appears
// in user source, which was the whole point of binding a class.
//
// Returns the number of chunks emitted; `seen` deduplicates (plan, family)
// across drain rounds and across the two drain sites.
static int render_deem_plan_chunks(const logos::compiler::lir::LProgram& prog,
                                   std::set<std::string>& seen) {
    int emitted = 0;
    for (const auto& inst : prog.deem_plan_insts) {
        const logos::compiler::lir::LProgram::DeemPlan* pp = nullptr;
        for (const auto& q : prog.deem_plans)
            if (q.name == inst.name && q.package == inst.package) { pp = &q; break; }
        if (!pp) continue;
        const auto& p = *pp;
        const std::string& family = inst.family;
        std::string key = p.package + "::" + p.name + "@" + family;
        if (!seen.insert(key).second) continue;
        // Re-spell the class parameter; every other parameter rides verbatim.
        std::string params;
        {
            std::string_view t = p.params_text;
            int depth = 0; size_t seg = 0;
            auto flush = [&](size_t end) {
                std::string_view piece = t.substr(seg, end - seg);
                size_t a = piece.find_first_not_of(" \t");
                if (a == std::string_view::npos) return;
                piece = piece.substr(a);
                size_t c = piece.find(':');
                std::string_view nm = c == std::string_view::npos ? piece
                                                                  : piece.substr(0, c);
                while (!nm.empty() && nm.back() == ' ') nm.remove_suffix(1);
                if (!params.empty()) params += ", ";
                if (nm == p.param_name) {
                    params += p.param_name;
                    params += ": &";
                    params += family;
                } else {
                    params += std::string(piece);
                }
            };
            for (size_t i = 0; i < t.size(); ++i) {
                char ch = t[i];
                if (ch == '(' || ch == '[' || ch == '<') ++depth;
                else if (ch == ')' || ch == ']' || ch == '>') --depth;
                else if (ch == ',' && depth == 0) { flush(i); seg = i + 1; }
            }
            flush(t.size());
        }
        // The binding is DECIDED in Logos, not here, and by DEEM, not by
        // Canon. Which container operations a query should use is a join of its
        // demands with the class's capabilities, and neither is readable from
        // C++: the wql AST has no node constants on this side, and Canon's
        // verdicts are computed in the handler. So this renders a metacall to
        // the query compiler and hands over the pieces — the query, the family
        // it runs against, and the class declaration whose facts the verdicts
        // come from. Canon supplies those facts; it does not own the binding.
        std::string chunk;
        chunk += "package logos.gen;\n";
        chunk += "use logos.std.wql.deem_bind;\n";
        chunk += "metacall __deem_bind(\"";
        chunk += esc_lit(p.is_pub ? p.name : ("-" + p.name));
        chunk += "\", \"";
        chunk += esc_lit(params);
        chunk += "\", \"";
        chunk += esc_lit(p.query_text);
        chunk += "\", \"";
        chunk += esc_lit(family);
        chunk += "\", \"";
        chunk += esc_lit(p.class_spec);
        chunk += "\", \"";
        // The family's CONFIG: the class spec carries the declaration's type
        // PARAMETERS, and a producer emitted against the family has to spell
        // what the family concretely is.
        {
            uint64_t h = 0;
            if (family.size() > 2)
                h = std::strtoull(family.c_str() + 2, nullptr, 16);
            auto cit = prog.wstatic_sources.find(h);
            chunk += esc_lit(cit == prog.wstatic_sources.end() ? std::string()
                                                              : cit->second);
        }
        chunk += "\");\n";
        if (logos_emit_source(chunk.c_str())) ++emitted;
    }
    return emitted;
}

// C++-side thunk staging (metacall sites, item thunks).
static int32_t logos_emit_source_thunk(const char* src) {
    return emit_source_tagged(src, "<metaprog-thunk>");
}

// Slice 3 of metaprog-quote (~/.claude/plans/metaprog-quote.md): item-level
// splice via Writ-bytes. The blob is a complete arena snapshot of a one-
// module Writ document (same shape as parser output). Host reconstructs
// it via from_bytes_copy and pushes onto the asts vector — sema's next
// iteration sees it like any other source file. Slice 4 will produce these
// bytes from quote_item! literals; for now Slice 3 has a test fixture that
// parses a string and hands its bytes back, just to exercise the seam.
//
// Dedup: hash (size, content) so a handler that always emits the same
// blob converges in 2 iters (mirrors logos_emit_source's string dedup).
extern "C" int32_t logos_emit_item_blob(const uint8_t* data, uint64_t size) {
    if (!g_any_emitted || !g_asts || !g_filenames || !g_from_binary
        || !data || size == 0) return 0;
    static std::set<std::string> blob_seen;
    std::string key(reinterpret_cast<const char*>(data), size);
    if (!blob_seen.insert(key).second) return 0;
    auto doc = logos::writ::from_bytes_copy(data, size);
    if (!doc) {
        std::fprintf(stderr, "logos_emit_item_blob: from_bytes_copy failed\n");
        blob_seen.erase(key);
        return 0;
    }
    g_asts->push_back(std::move(doc).get());
    g_filenames->emplace_back("<metaprog-blob>");
    g_from_binary->push_back(false);
    if (g_module_ids) g_module_ids->push_back(g_self_module_id);
    *g_any_emitted = true;
    record_emit_provenance();
    return 1;
}

// Slice 5a.1 of metaprog-quote: substitution variant of emit_item_blob.
// Reads a `QuoteItemBlob` POD struct from `blob_ptr`, snapshots the
// template into a fresh arena, walks the document's ITEMS array, and
// for every item-TOM that carries a `NAME_VAR` placeholder (an integer
// index, written by `lower_quote_item` in phase 3) replaces it with a
// freshly-allocated NAME string drawn from `idents[idx]`. Then forwards
// to the same `g_asts->push_back` path as `logos_emit_item_blob`.
//
// Layout of QuoteItemBlob (mirrors stdlib/std/compiler/metaprog/ast.logos):
//   { template_ptr: *const u8, template_size: u64, idents_blob: *const u8 }
// `idents_blob` is null or an owned heap allocation laid out as:
//   [u64 N] [N × IdentPod{ptr,len}] [packed bytes...]
// IdentPod.ptr is absolute and points into the same blob's byte section.
// Built by `qib_pack_idents` in the metaprog stdlib; lifetime owned by
// the calling thunk (freed via `qib_free_idents` after splice).
extern "C" int32_t logos_emit_item_blob_subst(const void* blob_ptr) {
    if (!g_any_emitted || !g_asts || !g_filenames || !g_from_binary
        || !blob_ptr) return 0;

    namespace la = logos::compiler::ast;
    using logos::writ::AnyVal;
    using logos::writ::ArenaString;
    using logos::writ::WritAccess;
    using logos::writ::ObjectArray;
    using logos::writ::TinyObjectMap;
    using logos::writ::TinyMapView;
    using logos::writ::ArrayView;
    using logos::writ::MapView;
    using logos::writ::as_tinymap;
    using logos::writ::as_array;
    using logos::writ::arena_offset_t;
    using logos::writ::copy_object_into;

    struct IdentPod { const uint8_t* ptr; uint64_t len; };
    struct BlobEntry { uint64_t offset; uint64_t size; };
    // T2-30 nested repeat: `inner_counts_offset==0` → flat depth-1 cursor
    // (`count` pods at `pods_offset`). Nonzero → ragged depth-2 cursor:
    // `count` = OUTER count O; `count` u64 sub-lengths live at
    // inner_counts_offset; pods_offset holds the FLATTENED inner idents
    // (CSR). Inner ident [o][n] = pods[prefix_sum(inner_counts,o)+n].
    struct CursorHdr { uint64_t count; uint64_t pods_offset; uint64_t inner_counts_offset; };
    struct BlobPod {
        const uint8_t* template_ptr;
        uint64_t template_size;
        const uint8_t* idents_blob;
        const uint8_t* blobs_blob;
        const uint8_t* cursors_blob;
    };
    const auto* blob = reinterpret_cast<const BlobPod*>(blob_ptr);
    if (!blob->template_ptr || blob->template_size == 0) return 0;

    uint64_t idents_count = 0;
    const IdentPod* idents = nullptr;
    if (blob->idents_blob) {
        idents_count = *reinterpret_cast<const uint64_t*>(blob->idents_blob);
        idents = reinterpret_cast<const IdentPod*>(blob->idents_blob + 8);
    }
    uint64_t blobs_count = 0;
    const BlobEntry* blob_entries = nullptr;
    if (blob->blobs_blob) {
        blobs_count = *reinterpret_cast<const uint64_t*>(blob->blobs_blob);
        blob_entries =
            reinterpret_cast<const BlobEntry*>(blob->blobs_blob + 8);
    }
    uint64_t cursors_count = 0;
    const CursorHdr* cursor_hdrs = nullptr;
    const uint8_t* cursors_base = nullptr;
    if (blob->cursors_blob) {
        cursors_count = *reinterpret_cast<const uint64_t*>(blob->cursors_blob);
        cursor_hdrs = reinterpret_cast<const CursorHdr*>(blob->cursors_blob + 8);
        cursors_base = blob->cursors_blob;
    }

    // Dedupe across iterations: hash template bytes plus each ident's
    // bytes plus each blob's body. Two calls that produce identical
    // output converge in 2 iters.
    static std::set<std::string> blob_seen;
    std::string key(reinterpret_cast<const char*>(blob->template_ptr),
                    blob->template_size);
    for (uint64_t i = 0; i < idents_count; ++i) {
        const auto& idp = idents[i];
        key.push_back('\x1f');  // unit-separator, can't appear in idents
        if (idp.ptr && idp.len) {
            key.append(reinterpret_cast<const char*>(idp.ptr), idp.len);
        }
    }
    for (uint64_t i = 0; i < blobs_count; ++i) {
        key.push_back('\x1e');
        const auto& be = blob_entries[i];
        if (be.size > 0) {
            key.append(reinterpret_cast<const char*>(blob->blobs_blob + be.offset),
                       be.size);
        }
    }
    for (uint64_t i = 0; i < cursors_count; ++i) {
        key.push_back('\x1d');
        const auto& ch = cursor_hdrs[i];
        const auto* pods = reinterpret_cast<const IdentPod*>(
            cursors_base + ch.pods_offset);
        // Total pod count: flat (depth-1) = count; ragged (depth-2) = Σ
        // inner_counts (count = OUTER count there). Both layouts store all
        // idents contiguously at pods_offset, so one linear sweep keys them.
        uint64_t total_pods = ch.count;
        if (ch.inner_counts_offset != 0) {
            const auto* ic = reinterpret_cast<const uint64_t*>(
                cursors_base + ch.inner_counts_offset);
            total_pods = 0;
            for (uint64_t o = 0; o < ch.count; ++o) total_pods += ic[o];
        }
        for (uint64_t j = 0; j < total_pods; ++j) {
            key.push_back('\x1c');
            if (pods[j].ptr && pods[j].len > 0) {
                key.append(reinterpret_cast<const char*>(pods[j].ptr),
                           pods[j].len);
            }
        }
    }
    if (!blob_seen.insert(key).second) {
        return 0;
    }

    auto doc_e = logos::writ::from_bytes_copy(blob->template_ptr,
                                                blob->template_size);
    if (!doc_e) {
        std::fprintf(stderr,
            "logos_emit_item_blob_subst: from_bytes_copy failed\n");
        blob_seen.erase(key);
        return 0;
    }
    auto doc = std::move(doc_e).get();

    // `doc` is a GrowableSingleChunk arena: an allocation that reallocs it
    // FREES the old chunk, dangling the raw pointers deep_copy_object /
    // expand_cursor_in_subtree hold into it mid-operation. Front-load a
    // single realloc NOW (nothing points into doc yet) to a size that bounds
    // the whole expansion, so no later allocation reallocs. Each repeat
    // iteration copies a body ≤ template_size, and total iterations across
    // all levels ≤ 2 × Σ(cursor pods); plus the substituted ident bytes.
    {
        uint64_t total_idents = 0, total_str = 0;
        for (uint64_t i = 0; i < cursors_count; ++i) {
            const auto& ch = cursor_hdrs[i];
            uint64_t tp = ch.count;
            if (ch.inner_counts_offset != 0) {
                const auto* ic = reinterpret_cast<const uint64_t*>(
                    cursors_base + ch.inner_counts_offset);
                tp = 0;
                for (uint64_t o = 0; o < ch.count; ++o) tp += ic[o];
            }
            const auto* pods = reinterpret_cast<const IdentPod*>(
                cursors_base + ch.pods_offset);
            total_idents += tp;
            for (uint64_t j = 0; j < tp; ++j) total_str += pods[j].len;
        }
        // total_str doubles as the blob-cursor byte budget: each pod is a
        // serialized doc spliced (copied in) exactly once, and the copied-in
        // object graph is bounded by ~its serialized size; 2× covers slack.
        //
        // ⚠ AND THE PLAIN `#(blob)` SPLICES HAVE TO BE IN THE BOUND TOO. Every
        // entry of `blobs_blob` is a serialized doc that `try_blob_splice` /
        // `try_inline_stmt_blobs` copies into `doc`, and its bytes are NOT part
        // of `template_size` — the template holds a placeholder. Omitting them
        // made the bound a function of the TEMPLATE only, so a quote whose
        // fragments are large (a join body of a few hundred statements) reallocd
        // the chunk mid-walk, dangled every raw pointer the walk holds, and
        // emitted an AST whose BODY offset points at freed memory: sema then
        // segfaulted in `map_of`, or — just under the threshold — reported "not
        // all paths return a value" about a body that plainly does. The
        // `--gen-dir` render of the same doc came out correct, which is what made
        // it read as a front-end bug rather than an arena one.
        uint64_t total_blob_bytes = 0;
        for (uint64_t i = 0; i < blobs_count; ++i) total_blob_bytes += blob_entries[i].size;
        size_t bound = static_cast<size_t>(blob->template_size)
                         * (2 * total_idents + 2)
                     + static_cast<size_t>(total_str) * 2
                     + static_cast<size_t>(total_blob_bytes) * 4 + 65536;
        if (!WritAccess::arena(doc).reserve(bound)) {
            std::fprintf(stderr,
                "logos_emit_item_blob_subst: reserve(%zu) failed — the quote's "
                "expansion does not fit\n", bound);
            blob_seen.erase(key);
            return 0;
        }
    }
    // The bound above is the ONLY thing standing between the walk and a realloc,
    // so it is CHECKED rather than trusted. What must not happen is the head chunk
    // MOVING: `doc` is GrowableSingleChunk, so growth reallocates the one chunk in
    // place of adding a second — `chunk_count()` stays 1 either way and says
    // nothing. The base pointer is the fact that matters, and if it changed then
    // every raw pointer the walk held was already freed.
    const uint8_t* doc_base_before = WritAccess::base(doc);

    // Substitute placeholders. Root is MODULE TOM with ITEMS array.
    auto root_off = WritAccess::root_offset(doc);
    auto root_ptr = [&]() {
        return TinyMapView(arena_offset_t(root_off.value()), doc.holder());
    };
    // Recursive walker: every TOM with NAME_VAR(int idx) gets the slot
    // replaced by NAME(string) drawn from idents[idx]. Mirrors the dst
    // walker in lower_quote_item, so placeholders at any nesting depth
    // (struct field name, fn arg name, type ref name, …) are resolved.
    namespace lh = logos::writ;
    bool subst_failed = false;

    // Splice helper: if `child_off` is a TOM placeholder with NAME_VAR(neg
    // idx), deep-copy the corresponding ExprBlob's root expr into `doc`
    // and call `replace_in_parent(new_off)` to point the parent's slot at
    // the spliced subtree. Returns true iff a splice was attempted (caller
    // skips recursion). Sets `subst_failed` on any internal error.
    auto try_blob_splice = [&](uint32_t child_off,
                               std::function<void(uint32_t)> replace_in_parent)
            -> bool {
        if (blobs_count == 0) return false;
        auto ctom = logos::writ::TinyMapView(arena_offset_t(child_off), doc.holder());
        if (!ctom.has_key(la::NAME_VAR.code)) return false;
        AnyVal nv = ctom.get(la::NAME_VAR.code);
        if (!nv.is_value()) return false;
        int32_t enc = nv.as_value<int32_t>();
        if (enc >= 0) return false;
        uint64_t bidx = static_cast<uint64_t>(-enc - 1);
        if (bidx >= blobs_count) {
            std::fprintf(stderr,
                "logos_emit_item_blob_subst: blob idx %llu out of range (count=%llu)\n",
                (unsigned long long)bidx,
                (unsigned long long)blobs_count);
            subst_failed = true; return true;
        }
        const auto& be = blob_entries[bidx];
        if (be.size == 0) {
            std::fprintf(stderr,
                "logos_emit_item_blob_subst: blob[%llu] empty\n",
                (unsigned long long)bidx);
            subst_failed = true; return true;
        }
        auto inner_e = logos::writ::from_bytes_copy(
            blob->blobs_blob + be.offset, be.size);
        if (!inner_e) {
            std::fprintf(stderr,
                "logos_emit_item_blob_subst: blob[%llu] from_bytes_copy failed\n",
                (unsigned long long)bidx);
            subst_failed = true; return true;
        }
        auto inner_doc = std::move(inner_e).get();
        const uint8_t* ib = WritAccess::base(inner_doc);
        auto inner_root_off = WritAccess::root_offset(inner_doc).value();
        const void* inner_root = ib + inner_root_off;
        auto cp_e = copy_object_into(inner_root, ib, doc);
        if (!cp_e) {
            std::fprintf(stderr,
                "logos_emit_item_blob_subst: copy_object_into failed\n");
            subst_failed = true; return true;
        }
        void* dst_obj = cp_e.get();
        uint32_t dst_off = static_cast<uint32_t>(
            reinterpret_cast<uint8_t*>(dst_obj) - WritAccess::base(doc));
        replace_in_parent(dst_off);
        return true;
    };

    // Blob-cursor splice (Vec<ExprBlob> cursor, encoding bit 0x800000): if
    // the TOM at `noff` is a blob-cursor placeholder whose pod resolves at
    // repeat iteration `iter`, deep-copy that pod's serialized expr into
    // `doc` and return the copy's offset (caller repoints the parent slot).
    // Returns 0 when `noff` is not a blob-cursor placeholder or the pod is
    // out of range; sets subst_failed on a real error.
    auto try_cursor_blob_splice = [&](uint32_t noff, uint64_t iter) -> uint32_t {
        if (cursors_count == 0) return 0;
        auto ntom = logos::writ::TinyMapView(arena_offset_t(noff), doc.holder());
        if (!ntom.has_key(la::NAME_VAR.code)) return 0;
        AnyVal nv = ntom.get(la::NAME_VAR.code);
        if (!nv.is_value()) return 0;
        int32_t enc = nv.as_value<int32_t>();
        if (enc < 0 || (enc & 0x400000) == 0 || (enc & 0x800000) == 0) return 0;
        int32_t cidx = enc & 0xFF;
        if (static_cast<uint64_t>(cidx) >= cursors_count) return 0;
        const auto& ch = cursor_hdrs[cidx];
        if (iter >= ch.count) return 0;
        const auto* pods = reinterpret_cast<const IdentPod*>(
            cursors_base + ch.pods_offset);
        const auto& pod = pods[iter];
        if (!pod.ptr || pod.len == 0) {
            std::fprintf(stderr,
                "logos_emit_item_blob_subst: blob cursor %d pod %llu empty\n",
                cidx, (unsigned long long)iter);
            subst_failed = true; return 0;
        }
        auto inner_e = logos::writ::from_bytes_copy(pod.ptr, pod.len);
        if (!inner_e) {
            std::fprintf(stderr,
                "logos_emit_item_blob_subst: blob cursor from_bytes_copy failed\n");
            subst_failed = true; return 0;
        }
        auto inner_doc = std::move(inner_e).get();
        const uint8_t* ib = WritAccess::base(inner_doc);
        auto inner_root_off = WritAccess::root_offset(inner_doc).value();
        auto cp_e = copy_object_into(ib + inner_root_off, ib, doc);
        if (!cp_e) {
            std::fprintf(stderr,
                "logos_emit_item_blob_subst: blob cursor copy_object_into failed\n");
            subst_failed = true; return 0;
        }
        return static_cast<uint32_t>(
            reinterpret_cast<uint8_t*>(cp_e.get()) - WritAccess::base(doc));
    };

    // Cursor expansion (item-level REPEAT_GROUP): substitutes
    // NAME_VAR(int with bit 30 set) → NAME(cursor_hdrs[idx].pods[iter])
    // throughout a freshly cloned subtree. Cursor placeholders only —
    // ident/blob placeholders in the body remain for the outer subst_walk.
    // Blob-cursor placeholders (bit 0x800000) are spliced from the PARENT
    // side (slot replacement) via try_cursor_blob_splice.
    std::function<void(uint32_t, uint64_t)> expand_cursor_in_subtree;
    expand_cursor_in_subtree = [&](uint32_t off, uint64_t iter) {
        uint8_t* dbase = WritAccess::base(doc);
        auto tom = logos::writ::TinyMapView(arena_offset_t(off), doc.holder());
        if (tom.has_key(la::NAME_VAR.code)) {
            AnyVal nv = tom.get(la::NAME_VAR.code);
            if (nv.is_value()) {
                int32_t enc = nv.as_value<int32_t>();
                if (enc >= 0 && (enc & 0x400000) != 0
                    && (enc & 0x800000) == 0) {
                    // Encoding: bit 22 = is-cursor, bit 23 = blob cursor
                    // (spliced by the parent, skipped here), bits 0-7 =
                    // cursor index, bits 8-21 = pinned-outer-index + 1
                    // (0 = unpinned).
                    // T2-30: a depth-2 cursor (inner_counts_offset != 0) seen
                    // unpinned during OUTER expansion records its outer index
                    // (iter) into bits 8-21 and stays a placeholder; the inner
                    // REPEAT_GROUP it sits in is expanded later by subst_walk,
                    // at which point the pin selects the right inner sublist.
                    int32_t cidx = enc & 0xFF;
                    int32_t pin  = (enc >> 8) & 0x3FFF;   // 0 = unpinned
                    int64_t pod_idx = -1;
                    if (static_cast<uint64_t>(cidx) < cursors_count) {
                        const auto& chh = cursor_hdrs[cidx];
                        if (chh.inner_counts_offset == 0) {
                            // depth-1 cursor: indexed by the current repeat iter.
                            if (iter < chh.count) pod_idx = (int64_t)iter;
                        } else if (pin == 0) {
                            // depth-2, outer phase: pin outer = iter, keep node.
                            if (iter + 1 <= 0x3FFF) {
                                int32_t pinned = enc | (int32_t)((iter + 1) << 8);
                                (void)tom.put(la::NAME_VAR.code,
                                    AnyVal::from_value<int32_t>(pinned));
                            }
                            // fall through: no substitution this phase.
                        } else {
                            // depth-2, inner phase: outer = pin-1, inner = iter.
                            const auto* ic = reinterpret_cast<const uint64_t*>(
                                cursors_base + chh.inner_counts_offset);
                            uint64_t o = (uint64_t)(pin - 1);
                            if (o < chh.count && iter < ic[o]) {
                                uint64_t base = 0;
                                for (uint64_t t = 0; t < o; ++t) base += ic[t];
                                pod_idx = (int64_t)(base + iter);
                            }
                        }
                    }
                    if (pod_idx >= 0) {
                        const auto& ch = cursor_hdrs[cidx];
                        {
                            const auto* pods = reinterpret_cast<const IdentPod*>(
                                cursors_base + ch.pods_offset);
                            const auto& pod = pods[pod_idx];
                            auto str_e = doc.make_string(
                                std::string_view(
                                    reinterpret_cast<const char*>(pod.ptr),
                                    pod.len));
                            if (str_e) {
                                uint32_t name_off = str_e->offset().value();
                                dbase = WritAccess::base(doc);
                                tom = logos::writ::TinyMapView(arena_offset_t(off), doc.holder());
                                // LIT_STR placeholder (`##field`) → ident text
                                // into VALUE (str label); VAR_REF → NAME.
                                // (T2-22 str-position antiquot, repeat path.)
                                bool is_strlit = false;
                                if (tom.has_key(la::CODE.code)) {
                                    AnyVal cv = tom.get(la::CODE.code);
                                    if (cv.is_value() && cv.as_value<int32_t>() == la::LIT_STR.code)
                                        is_strlit = true;
                                }
                                (void)tom.put(is_strlit ? la::VALUE.code : la::NAME.code,
                                    AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(name_off)));
                                dbase = WritAccess::base(doc);
                                tom = logos::writ::TinyMapView(arena_offset_t(off), doc.holder());
                                tom.remove(la::NAME_VAR.code);
                            }
                        }
                    }
                }
            }
        }
        // Snapshot children before recursion (puts may have rebased). Keys
        // (for TOM children) and indices (for array elements) ride along so
        // a blob-cursor splice can repoint the parent slot in place.
        struct Child { bool is_arr; uint8_t key; uint32_t off; };
        std::vector<Child> children;
        dbase = WritAccess::base(doc);
        tom = logos::writ::TinyMapView(arena_offset_t(off), doc.holder());
        uint64_t bm = tom.bitmap();
        for (uint8_t key = 0; key < TinyObjectMap::MAX_KEYS; ++key) {
            if (!(bm & (1ULL << key))) continue;
            if (key == la::NAME_VAR.code) continue;
            AnyVal av = tom.get(key);
            if (av.is_null() || !av.is_pointer()) continue;
            uint32_t coff = static_cast<uint32_t>(av.to_offset(WritAccess::base(doc)).value());
            const uint8_t* pointee = dbase + coff;
            lh::TypeTag tag = lh::TypeTag::read_before(pointee);
            if (tag.type_code() == lh::type_hash::TinyObjectMap)
                children.push_back({false, key, coff});
            else if (tag.type_code() == lh::type_hash::Array)
                children.push_back({true, key, coff});
        }
        for (auto [is_arr, key, coff] : children) {
            if (subst_failed) return;
            if (!is_arr) {
                uint32_t sp = try_cursor_blob_splice(coff, iter);
                if (subst_failed) return;
                if (sp != 0) {
                    auto ptom = logos::writ::TinyMapView(
                        arena_offset_t(off), doc.holder());
                    (void)ptom.put(key, AnyVal::from_offset(
                        WritAccess::base(doc), arena_offset_t(sp)));
                    continue;
                }
                expand_cursor_in_subtree(coff, iter);
                continue;
            }
            uint8_t* db2 = WritAccess::base(doc);
            auto arr = logos::writ::ArrayView(arena_offset_t(coff), doc.holder());
            std::vector<std::pair<uint64_t, uint32_t>> elems;
            for (uint64_t i = 0; i < arr.size(); ++i) {
                AnyVal e = arr.get(i);
                if (e.is_null() || !e.is_pointer()) continue;
                uint32_t eoff = static_cast<uint32_t>(e.to_offset(WritAccess::base(doc)).value());
                const uint8_t* ep = db2 + eoff;
                lh::TypeTag etag = lh::TypeTag::read_before(ep);
                if (etag.type_code() == lh::type_hash::TinyObjectMap)
                    elems.push_back({i, eoff});
            }
            for (auto [ei, eoff] : elems) {
                uint32_t sp = try_cursor_blob_splice(eoff, iter);
                if (subst_failed) return;
                if (sp != 0) {
                    auto parr = logos::writ::ArrayView(
                        arena_offset_t(coff), doc.holder());
                    parr.set(ei, AnyVal::from_offset(
                        WritAccess::base(doc), arena_offset_t(sp)));
                    continue;
                }
                expand_cursor_in_subtree(eoff, iter);
            }
        }
    };

    // Find the cursor count to expand a REPEAT_GROUP body to. Walks the
    // body looking for any cursor-encoded NAME_VAR; uses cursor_hdrs[idx].count.
    // Errors if no cursor found (caller validated this at sema time).
    std::function<uint64_t(uint32_t)> find_cursor_count_in_body;
    find_cursor_count_in_body = [&](uint32_t off) -> uint64_t {
        uint8_t* dbase = WritAccess::base(doc);
        auto tom = logos::writ::TinyMapView(arena_offset_t(off), doc.holder());
        if (tom.has_key(la::NAME_VAR.code)) {
            AnyVal nv = tom.get(la::NAME_VAR.code);
            if (nv.is_value()) {
                int32_t enc = nv.as_value<int32_t>();
                if (enc >= 0 && (enc & 0x400000) != 0) {
                    int32_t cidx = enc & 0xFF;
                    int32_t pin  = (enc >> 8) & 0x3FFF;
                    if (static_cast<uint64_t>(cidx) < cursors_count) {
                        const auto& ch = cursor_hdrs[cidx];
                        // T2-30: a PINNED depth-2 cursor drives the INNER
                        // repeat count = its outer sublist length; otherwise
                        // (depth-1, or depth-2 at the outer level) the count
                        // is the cursor's outer dimension.
                        if (ch.inner_counts_offset != 0 && pin != 0) {
                            const auto* ic = reinterpret_cast<const uint64_t*>(
                                cursors_base + ch.inner_counts_offset);
                            uint64_t o = (uint64_t)(pin - 1);
                            if (o < ch.count) return ic[o];
                        }
                        return ch.count;
                    }
                }
            }
        }
        uint64_t bm = tom.bitmap();
        for (uint8_t key = 0; key < TinyObjectMap::MAX_KEYS; ++key) {
            if (!(bm & (1ULL << key))) continue;
            if (key == la::NAME_VAR.code) continue;
            AnyVal av = tom.get(key);
            if (av.is_null() || !av.is_pointer()) continue;
            uint32_t coff = static_cast<uint32_t>(av.to_offset(WritAccess::base(doc)).value());
            const uint8_t* pointee = dbase + coff;
            lh::TypeTag tag = lh::TypeTag::read_before(pointee);
            if (tag.type_code() == lh::type_hash::TinyObjectMap) {
                uint64_t n = find_cursor_count_in_body(coff);
                if (n != static_cast<uint64_t>(-1)) return n;
            } else if (tag.type_code() == lh::type_hash::Array) {
                auto arr = ArrayView(arena_offset_t(coff), doc.holder());
                for (uint64_t i = 0; i < arr.size(); ++i) {
                    AnyVal e = arr.get(i);
                    if (!e.is_pointer()) continue;
                    uint32_t eoff = static_cast<uint32_t>(e.to_offset(WritAccess::base(doc)).value());
                    const uint8_t* ep = WritAccess::base(doc) + eoff;
                    lh::TypeTag etag = lh::TypeTag::read_before(ep);
                    if (etag.type_code() == lh::type_hash::TinyObjectMap) {
                        uint64_t n = find_cursor_count_in_body(eoff);
                        if (n != static_cast<uint64_t>(-1)) return n;
                    }
                }
            }
        }
        return static_cast<uint64_t>(-1);
    };

    // Detects whether the array at `arr_off` contains any REPEAT_GROUP
    // element with sep ∈ {0, 1}. If so, builds a new ObjectArray with each
    // REPEAT_GROUP expanded N times (via deep-copy + cursor substitution)
    // and returns the new array's offset; otherwise returns 0.
    auto try_expand_array_repeats = [&](uint32_t arr_off) -> uint32_t {
        auto arr = logos::writ::ArrayView(arena_offset_t(arr_off), doc.holder());
        bool any_repeat = false;
        uint64_t n_src = arr.size();
        for (uint64_t i = 0; i < n_src; ++i) {
            AnyVal e = arr.get(i);
            if (!e.is_pointer()) continue;
            uint32_t eoff = static_cast<uint32_t>(e.to_offset(WritAccess::base(doc)).value());
            auto etom = logos::writ::TinyMapView(arena_offset_t(eoff), doc.holder());
            int32_t cd = 0;
            if (etom.has_key(la::CODE.code)) {
                AnyVal cav = etom.get(la::CODE.code);
                if (!cav.is_null() && !cav.is_pointer())
                    cd = cav.as_value<int32_t>();
            }
            if (cd == la::REPEAT_GROUP.code) {
                int32_t sep = -1;
                if (etom.has_key(la::OP.code)) {
                    AnyVal sav = etom.get(la::OP.code);
                    if (!sav.is_null() && !sav.is_pointer())
                        sep = sav.as_value<int32_t>();
                }
                if (sep == 0 || sep == 1) { any_repeat = true; break; }
            }
        }
        if (!any_repeat) return 0;
        // Snapshot all source element offsets.
        struct SrcEl { bool is_rep; uint32_t off; uint64_t body_off; };
        std::vector<SrcEl> src_els;
        std::vector<AnyVal> src_nonptr;
        for (uint64_t i = 0; i < n_src; ++i) {
            AnyVal e = arr.get(i);
            if (!e.is_pointer()) {
                src_els.push_back({false, 0, 0});
                src_nonptr.push_back(e);
                continue;
            }
            src_nonptr.push_back(AnyVal{});
            uint32_t eoff = static_cast<uint32_t>(e.to_offset(WritAccess::base(doc)).value());
            auto etom = logos::writ::TinyMapView(arena_offset_t(eoff), doc.holder());
            int32_t cd = 0;
            if (etom.has_key(la::CODE.code)) {
                AnyVal cav = etom.get(la::CODE.code);
                if (!cav.is_null() && !cav.is_pointer())
                    cd = cav.as_value<int32_t>();
            }
            if (cd == la::REPEAT_GROUP.code) {
                AnyVal bav = etom.get(la::VALUE.code);
                if (!bav.is_pointer()) {
                    subst_failed = true;
                    return 0;
                }
                src_els.push_back({true, 0,
                    bav.to_offset(WritAccess::base(doc)).value()});
            } else {
                src_els.push_back({false, eoff, 0});
            }
        }
        auto new_arr_e = doc.make_array( std::max<uint64_t>(4, n_src));
        if (!new_arr_e) { subst_failed = true; return 0; }
        uint32_t new_arr_off = static_cast<uint32_t>(
            new_arr_e->offset().value());
        for (uint64_t i = 0; i < src_els.size(); ++i) {
            auto na = ArrayView(arena_offset_t(new_arr_off), doc.holder());
            if (!src_els[i].is_rep) {
                if (src_els[i].off == 0) {
                    (void)na.push_back(src_nonptr[i]);
                } else {
                    (void)na.push_back(
                        AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(src_els[i].off))
                        );
                }
                continue;
            }
            uint32_t body_off = static_cast<uint32_t>(src_els[i].body_off);
            uint64_t n = find_cursor_count_in_body(body_off);
            if (n == static_cast<uint64_t>(-1)) {
                std::fprintf(stderr,
                    "logos_emit_item_blob_subst: REPEAT_GROUP body has no cursor\n");
                subst_failed = true; return 0;
            }
            for (uint64_t j = 0; j < n; ++j) {
                const uint8_t* base = WritAccess::base(doc);
                const void* src_obj = base + body_off;
                auto cp_e = copy_object_into(src_obj, base, doc);
                if (!cp_e) { subst_failed = true; return 0; }
                void* dst_obj = cp_e.get();
                uint32_t copy_off = static_cast<uint32_t>(
                    reinterpret_cast<uint8_t*>(dst_obj) - WritAccess::base(doc));
                // Body root that IS a bare blob-cursor placeholder
                // (`#( #frags )*`): splice replaces the whole copy.
                uint32_t sp = try_cursor_blob_splice(copy_off, j);
                if (subst_failed) return 0;
                if (sp != 0) copy_off = sp;
                else expand_cursor_in_subtree(copy_off, j);
                if (subst_failed) return 0;
                auto na2 = ArrayView(arena_offset_t(new_arr_off), doc.holder());
                (void)na2.push_back(
                    AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(copy_off)));
            }
        }
        return new_arr_off;
    };

    // Statement-list INLINE splice — the quote_item! twin of the rule in
    // logos_quote_expr_subst. `#(frag);` at statement position is an EXPR_STMT
    // whose VALUE is a blob placeholder; when the blob is BLOCK-rooted its
    // statements belong to THIS array, not to a nested block, so a run can
    // export the bindings the runs after it read.
    //
    // ⚠ This exists so the two quote flavours MEAN THE SAME THING. Without it
    // an emitter that moved a body from `quote_block!` into a `quote_item!`
    // template would silently gain a scope, and the failure surfaces far away
    // as "undefined variable" inside generated source.
    //
    // Returns a NEW array offset when anything was inlined, 0 otherwise. The
    // blob's statements are copied RAW (they were already substituted when the
    // fragment was built), matching try_blob_splice, which the caller skips
    // recursion for on the same grounds.
    auto try_inline_stmt_blobs = [&](uint32_t arr_off) -> uint32_t {
        if (blobs_count == 0) return 0;
        // `el` qualifies → returns the blob index, else -1.
        auto stmt_blob_idx = [&](uint32_t eoff) -> int64_t {
            auto etom = logos::writ::TinyMapView(arena_offset_t(eoff), doc.holder());
            int32_t cd = 0;
            if (etom.has_key(la::CODE.code)) {
                AnyVal cav = etom.get(la::CODE.code);
                if (!cav.is_null() && !cav.is_pointer()) cd = cav.as_value<int32_t>();
            }
            if (cd != la::EXPR_STMT.code) return -1;
            if (!etom.has_key(la::VALUE.code)) return -1;
            AnyVal vav = etom.get(la::VALUE.code);
            if (vav.is_null() || !vav.is_pointer()) return -1;
            uint32_t voff = static_cast<uint32_t>(
                vav.to_offset(WritAccess::base(doc)).value());
            auto vtom = logos::writ::TinyMapView(arena_offset_t(voff), doc.holder());
            if (!vtom.has_key(la::NAME_VAR.code)) return -1;
            AnyVal nv = vtom.get(la::NAME_VAR.code);
            if (!nv.is_value()) return -1;
            int32_t enc = nv.as_value<int32_t>();
            if (enc >= 0) return -1;
            uint64_t bidx = static_cast<uint64_t>(-enc - 1);
            if (bidx >= blobs_count) return -1;
            return static_cast<int64_t>(bidx);
        };
        auto arr0 = logos::writ::ArrayView(arena_offset_t(arr_off), doc.holder());
        uint64_t n_src = arr0.size();
        std::vector<std::pair<AnyVal, int64_t>> els;   // (elem, blob idx or -1)
        bool any = false;
        for (uint64_t i = 0; i < n_src; ++i) {
            AnyVal e = arr0.get(i);
            if (!e.is_pointer()) { els.push_back({e, -1}); continue; }
            uint32_t eoff = static_cast<uint32_t>(
                e.to_offset(WritAccess::base(doc)).value());
            int64_t bi = stmt_blob_idx(eoff);
            els.push_back({e, bi});
            if (bi >= 0) any = true;
        }
        if (!any) return 0;
        auto new_arr_e = doc.make_array(std::max<uint64_t>(4, n_src));
        if (!new_arr_e) { subst_failed = true; return 0; }
        uint32_t new_arr_off = static_cast<uint32_t>(new_arr_e->offset().value());
        for (auto& [el, bi] : els) {
            auto na = [&]() {
                return ArrayView(arena_offset_t(new_arr_off), doc.holder());
            };
            if (bi < 0) { (void)na().push_back(el); continue; }
            const auto& be = blob_entries[bi];
            if (be.size == 0) {
                std::fprintf(stderr,
                    "logos_emit_item_blob_subst: blob[%lld] empty\n",
                    (long long)bi);
                subst_failed = true; return 0;
            }
            auto inner_e = logos::writ::from_bytes_copy(
                blob->blobs_blob + be.offset, be.size);
            if (!inner_e) {
                std::fprintf(stderr,
                    "logos_emit_item_blob_subst: blob[%lld] from_bytes_copy failed\n",
                    (long long)bi);
                subst_failed = true; return 0;
            }
            auto inner_doc = std::move(inner_e).get();
            const uint8_t* ib = WritAccess::base(inner_doc);
            uint32_t inner_root = static_cast<uint32_t>(
                WritAccess::root_offset(inner_doc).value());
            auto rt = logos::writ::TinyMapView(
                arena_offset_t(inner_root), inner_doc.holder());
            int32_t rcd = 0;
            if (rt.has_key(la::CODE.code)) {
                AnyVal cav = rt.get(la::CODE.code);
                if (!cav.is_null() && !cav.is_pointer()) rcd = cav.as_value<int32_t>();
            }
            if (rcd != la::BLOCK.code) {
                // Not a statement list — leave the placeholder for the
                // ordinary blob path, which splices the root in place.
                (void)na().push_back(el);
                continue;
            }
            if (!rt.has_key(la::ITEMS.code)) continue;   // `{}` — nothing
            AnyVal iav = rt.get(la::ITEMS.code);
            if (iav.is_null() || !iav.is_pointer()) continue;
            uint32_t items_off = static_cast<uint32_t>(iav.to_offset(ib).value());
            auto items = logos::writ::ArrayView(
                arena_offset_t(items_off), inner_doc.holder());
            uint64_t n_items = items.size();
            for (uint64_t j = 0; j < n_items; ++j) {
                AnyVal se = items.get(j);
                if (se.is_null() || !se.is_pointer()) continue;
                uint32_t so = static_cast<uint32_t>(se.to_offset(ib).value());
                auto cp_e = copy_object_into(ib + so, ib, doc);
                if (!cp_e) {
                    std::fprintf(stderr,
                        "logos_emit_item_blob_subst: stmt-list copy failed\n");
                    subst_failed = true; return 0;
                }
                uint32_t co = static_cast<uint32_t>(
                    reinterpret_cast<uint8_t*>(cp_e.get()) - WritAccess::base(doc));
                (void)na().push_back(
                    AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(co)));
            }
        }
        return new_arr_off;
    };

    // `pub(#v)` visibility antiquote: a VIS sub-node carrying an int-encoded
    // Ident placeholder. Resolved from the CHILD LOOP of subst_walk (the only
    // place that knows the parent item) — works at any depth (top-level items,
    // impl/trait methods). The generic walk can't handle it: it errors on
    // empty idents and would route the text into VIS.NAME verbatim. Semantics
    // (the ident IS the visibility syntax):
    //   ""       → private   (IS_PUB←0; sema reads IS_PUB by VALUE)
    //   "pub"    → plain pub (VIS left NAME-less)
    //   anything → VIS.NAME  (sema validates; "module" → pub(module))
    auto resolve_vis_child = [&](uint32_t item_off, uint32_t vis_off) {
        auto vis = [&]() {
            return logos::writ::TinyMapView(arena_offset_t(vis_off), doc.holder());
        };
        if (!vis().has_key(la::NAME_VAR.code)) return;
        AnyVal idx_av = vis().get(la::NAME_VAR.code);
        if (!idx_av.is_value()) return;
        int32_t idx = idx_av.as_value<int32_t>();
        if (idx < 0 || (idx & 0x400000)
            || static_cast<uint64_t>(idx) >= idents_count) {
            std::fprintf(stderr,
                "logos_emit_item_blob_subst: vis placeholder idx %d invalid\n",
                idx);
            subst_failed = true; return;
        }
        const auto& idp = idents[idx];
        std::string w;
        if (idp.ptr && idp.len > 0)
            w.assign(reinterpret_cast<const char*>(idp.ptr), idp.len);
        vis().remove(la::NAME_VAR.code);
        if (w.empty()) {
            (void)logos::writ::TinyMapView(arena_offset_t(item_off), doc.holder())
                .put(la::IS_PUB.code, AnyVal::from_value(int32_t(0)));
            return;
        }
        if (w == "pub") return;
        auto str_e = doc.make_string(std::string_view(w));
        if (!str_e) {
            std::fprintf(stderr,
                "logos_emit_item_blob_subst: vis name alloc failed\n");
            subst_failed = true; return;
        }
        uint32_t name_off = static_cast<uint32_t>(str_e->offset().value());
        (void)vis().put(la::NAME.code,
            AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(name_off)));
    };

    std::function<void(uint32_t)> subst_walk = [&](uint32_t off) {
        if (subst_failed) return;
        uint8_t* dbase = WritAccess::base(doc);
        auto tom = logos::writ::TinyMapView(arena_offset_t(off), doc.holder());
        if (tom.has_key(la::NAME_VAR.code)) {
            AnyVal idx_av = tom.get(la::NAME_VAR.code);
            if (idx_av.is_value()) {
                int32_t idx = idx_av.as_value<int32_t>();
                if (idx < 0) {
                    // Blob placeholder; expected to be spliced at parent
                    // level. Reaching it directly means it sits at a
                    // position where blob splice doesn't apply.
                    std::fprintf(stderr,
                        "logos_emit_item_blob_subst: stray blob placeholder (idx=%d)\n",
                        idx);
                    subst_failed = true; return;
                }
                if (static_cast<uint64_t>(idx) >= idents_count) {
                    std::fprintf(stderr,
                        "logos_emit_item_blob_subst: ident idx %d out of range (count=%llu)\n",
                        idx, (unsigned long long)idents_count);
                    subst_failed = true; return;
                }
                const auto& idp = idents[idx];
                if (!idp.ptr || idp.len == 0) {
                    std::fprintf(stderr,
                        "logos_emit_item_blob_subst: ident[%d] is empty\n", idx);
                    subst_failed = true; return;
                }
                auto str_e = doc.make_string(
                    std::string_view(reinterpret_cast<const char*>(idp.ptr),
                                     idp.len));
                if (!str_e) {
                    std::fprintf(stderr,
                        "logos_emit_item_blob_subst: ArenaString alloc failed\n");
                    subst_failed = true; return;
                }
                uint32_t name_off = static_cast<uint32_t>(
                    str_e->offset().value());
                dbase = WritAccess::base(doc);
                tom = logos::writ::TinyMapView(arena_offset_t(off), doc.holder());
                // Where the substituted ident text lands depends on the node:
                //   • LIT_STR (`##name`) → VALUE (a string-literal label);
                //   • STATIC_CALL (`#T::method(args)`) → RECEIVER (the receiver
                //     TYPE — NAME already holds the literal method name);
                //   • everything else (VAR_REF `#name`, struct/fn/type NAME_VAR) → NAME.
                uint8_t target_slot = la::NAME.code;
                if (tom.has_key(la::CODE.code)) {
                    AnyVal cv = tom.get(la::CODE.code);
                    if (cv.is_value()) {
                        int32_t nc = cv.as_value<int32_t>();
                        if (nc == la::LIT_STR.code)          target_slot = la::VALUE.code;
                        else if (nc == la::STATIC_CALL.code) target_slot = la::RECEIVER.code;
                        // rel_bind `rel r = #fn;` — the materializer fn ident
                        // lives in VALUE (NAME already holds the literal rel name).
                        else if (nc == la::REL_BIND.code)    target_slot = la::VALUE.code;
                    }
                }
                (void)tom.put(target_slot,
                    AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(name_off)));
                dbase = WritAccess::base(doc);
                tom = logos::writ::TinyMapView(arena_offset_t(off), doc.holder());
                tom.remove(la::NAME_VAR.code);
            }
        }
        // `pub(#v)` visibility antiquote — consume BEFORE walking children.
        // The parser's `$...` collector ALIASES the pub_vis node into this
        // node's ITEMS array (systemic junk; consumers skip CODE-less
        // entries), and ITEMS' key (2) sorts before VIS (10) — the generic
        // walk would reach the alias first and put the ident into VIS.NAME
        // verbatim (→ `pub(pub)`). Resolving here empties the node's
        // NAME_VAR, so both later reachings (VIS slot + ITEMS alias) no-op.
        // Discriminator vs ELSE (same slot 10): a VIS node is a CODE-less
        // TOM holding an ident placeholder (idx ≥ 0); an else-branch is a
        // BLOCK/IF (has CODE).
        dbase = WritAccess::base(doc);
        tom = logos::writ::TinyMapView(arena_offset_t(off), doc.holder());
        if (tom.has_key(la::VIS.code)) {
            AnyVal vav = tom.get(la::VIS.code);
            if (!vav.is_null() && vav.is_pointer()) {
                uint32_t vis_off = static_cast<uint32_t>(
                    vav.to_offset(WritAccess::base(doc)).value());
                lh::TypeTag vtag = lh::TypeTag::read_before(dbase + vis_off);
                if (vtag.type_code() == lh::type_hash::TinyObjectMap) {
                    auto vtom = logos::writ::TinyMapView(
                        arena_offset_t(vis_off), doc.holder());
                    if (!vtom.has_key(la::CODE.code)
                        && vtom.has_key(la::NAME_VAR.code)) {
                        AnyVal iv = vtom.get(la::NAME_VAR.code);
                        if (iv.is_value() && iv.as_value<int32_t>() >= 0) {
                            resolve_vis_child(off, vis_off);
                            if (subst_failed) return;
                        }
                    }
                }
            }
        }
        // Snapshot children before recursion (puts above may have rebased).
        // Track key for TOM children and elem-index for array children so
        // blob splice can rewrite the parent's slot.
        struct ChildRef { bool is_arr; uint8_t key; uint32_t coff; };
        std::vector<ChildRef> children;
        dbase = WritAccess::base(doc);
        tom = logos::writ::TinyMapView(arena_offset_t(off), doc.holder());
        uint64_t bm = tom.bitmap();
        for (uint8_t key = 0; key < TinyObjectMap::MAX_KEYS; ++key) {
            if (!(bm & (1ULL << key))) continue;
            if (key == la::NAME_VAR.code) continue;
            AnyVal av = tom.get(key);
            if (av.is_null() || !av.is_pointer()) continue;
            uint32_t coff = static_cast<uint32_t>(av.to_offset(WritAccess::base(doc)).value());
            const uint8_t* pointee = dbase + coff;
            lh::TypeTag tag = lh::TypeTag::read_before(pointee);
            if (tag.type_code() == lh::type_hash::TinyObjectMap)
                children.push_back({false, key, coff});
            else if (tag.type_code() == lh::type_hash::Array)
                children.push_back({true, key, coff});
        }
        for (auto& cref : children) {
            if (subst_failed) return;
            if (!cref.is_arr) {
                uint8_t key = cref.key;
                bool spliced = try_blob_splice(cref.coff,
                    [&](uint32_t new_off) {
                        auto t = logos::writ::TinyMapView(arena_offset_t(off), doc.holder());
                        (void)t.put(key,
                            AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(new_off)));
                    });
                if (!spliced) subst_walk(cref.coff);
                continue;
            }
            // Array child: pre-expand any REPEAT_GROUP elements (cursor
            // splice), then iterate elements as placeholders / sub-trees.
            uint32_t arr_off = cref.coff;
            if (cursors_count > 0) {
                uint32_t expanded = try_expand_array_repeats(arr_off);
                if (subst_failed) return;
                if (expanded != 0) {
                    // Replace parent's slot with the new array.
                    auto t = logos::writ::TinyMapView(arena_offset_t(off), doc.holder());
                    (void)t.put(cref.key,
                        AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(expanded)));
                    arr_off = expanded;
                }
            }
            // …then flatten any `#(frag);` whose fragment is a statement LIST.
            // After the repeat expansion, so a `#( #(f); )*` fold flattens too.
            {
                uint32_t inlined = try_inline_stmt_blobs(arr_off);
                if (subst_failed) return;
                if (inlined != 0) {
                    auto t = logos::writ::TinyMapView(arena_offset_t(off), doc.holder());
                    (void)t.put(cref.key,
                        AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(inlined)));
                    arr_off = inlined;
                }
            }
            uint8_t* dbase2 = WritAccess::base(doc);
            auto arr = logos::writ::ArrayView(arena_offset_t(arr_off), doc.holder());
            std::vector<std::pair<uint64_t, uint32_t>> elems;
            for (uint64_t i = 0; i < arr.size(); ++i) {
                AnyVal e = arr.get(i);
                if (e.is_null() || !e.is_pointer()) continue;
                uint32_t eoff = static_cast<uint32_t>(e.to_offset(WritAccess::base(doc)).value());
                const uint8_t* ep = dbase2 + eoff;
                lh::TypeTag etag = lh::TypeTag::read_before(ep);
                if (etag.type_code() == lh::type_hash::TinyObjectMap)
                    elems.push_back({i, eoff});
            }
            for (auto [ei, eoff] : elems) {
                if (subst_failed) return;
                bool spliced = try_blob_splice(eoff,
                    [&](uint32_t new_off) {
                        auto a = logos::writ::ArrayView(arena_offset_t(arr_off), doc.holder());
                        a.set(ei,
                               AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(new_off))
                               );
                    });
                if (!spliced) subst_walk(eoff);
            }
        }
    };
    if (root_ptr().has_key(la::ITEMS.code)) {
        AnyVal items_av = root_ptr().get(la::ITEMS.code);
        if (!items_av.is_null()) {
            auto items_off = items_av.to_offset(WritAccess::base(doc));
            auto items_ptr = [&]() {
                return ArrayView(arena_offset_t(items_off.value()), doc.holder());
            };
            uint64_t n = items_ptr().size();
            for (uint64_t i = 0; i < n; ++i) {
                AnyVal it_av = items_ptr().get(i);
                if (it_av.is_null() || !it_av.is_pointer()) continue;
                subst_walk(static_cast<uint32_t>(it_av.to_offset(WritAccess::base(doc)).value()));
                if (subst_failed) {
                    blob_seen.erase(key); return 0;
                }
            }
        }
    }

    // Synth USES pass. subst_walk above covers ITEMS only, but a quote body may
    // carry a dynamic import `use #pkg;` (USE with a NAME_VAR placeholder). Walk
    // the synth USES array to (a) substitute those placeholders NAME_VAR→NAME
    // like any other, and (b) DROP an optional import `use #pkg?;` whose package
    // ident is empty (IS_OPTIONAL marker) — the mechanism that lets an emitter
    // carry a conditional import without a blob-variant per present/absent case.
    // Runs BEFORE the user-USES merge below so dedup sees resolved names.
    {
        AnyVal uav = root_ptr().get(la::USES.code);
        if (!uav.is_null() && uav.is_pointer()) {
            uint32_t uarr_off = static_cast<uint32_t>(
                uav.to_offset(WritAccess::base(doc)).value());
            // `#( use #us; )*` — a RUNTIME-SIZED import list. USES is the
            // module root's own array, so it is not reached by the child walk
            // that expands repeats inside items; expand it here, or the group
            // survives as a CODE-less node that build_import_scope skips and
            // the whole list vanishes without a diagnostic.
            if (cursors_count > 0) {
                uint32_t expanded = try_expand_array_repeats(uarr_off);
                if (subst_failed) { blob_seen.erase(key); return 0; }
                if (expanded != 0) {
                    (void)root_ptr().put(la::USES.code,
                        AnyVal::from_offset(WritAccess::base(doc),
                                            arena_offset_t(expanded)));
                    uarr_off = expanded;
                }
            }
            uint64_t un0 = ArrayView(arena_offset_t(uarr_off), doc.holder()).size();
            std::vector<uint32_t> survivors;
            bool dropped = false;
            for (uint64_t i = 0; i < un0; ++i) {
                AnyVal e = ArrayView(arena_offset_t(uarr_off), doc.holder()).get(i);
                if (e.is_null() || !e.is_pointer()) continue;
                uint32_t eoff = static_cast<uint32_t>(
                    e.to_offset(WritAccess::base(doc)).value());
                auto etom = logos::writ::TinyMapView(arena_offset_t(eoff), doc.holder());
                if (etom.has_key(la::NAME_VAR.code)) {
                    AnyVal nvav = etom.get(la::NAME_VAR.code);
                    if (!nvav.is_value()) {
                        // An UNINDEXED placeholder: `lower_quote_item` failed
                        // to number this node, so nothing can substitute it.
                        // ⚠ This used to DROP the node on the theory that the
                        // same use was aliased into ITEMS and indexed there —
                        // it is not (`quote_item_expr` captures USES and ITEMS
                        // as two disjoint arrays, no `$...`), so the theory
                        // silently deleted every antiquoted import. Fail loudly
                        // instead: a missing import surfaces as an unresolved
                        // name in generated code, which names neither.
                        std::fprintf(stderr,
                            "logos_emit_item_blob_subst: `use #pkg;` with no "
                            "placeholder index (quote lowering did not number "
                            "the module's USES)\n");
                        blob_seen.erase(key);
                        return 0;
                    }
                    int32_t nidx = nvav.as_value<int32_t>();
                    bool optional = etom.has_key(la::IS_OPTIONAL.code);
                    if (optional && nidx >= 0
                        && static_cast<uint64_t>(nidx) < idents_count
                        && (!idents[nidx].ptr || idents[nidx].len == 0)) {
                        dropped = true;   // `use #pkg?;` with empty pkg → omit
                        continue;
                    }
                    subst_walk(eoff);         // NAME_VAR → NAME (dotted path)
                    if (subst_failed) { blob_seen.erase(key); return 0; }
                }
                survivors.push_back(eoff);
            }
            if (dropped) {
                auto na_e = doc.make_array(std::max<uint64_t>(1, survivors.size()));
                if (na_e) {
                    uint32_t na_off = static_cast<uint32_t>(na_e->offset().value());
                    for (uint32_t so : survivors) {
                        (void)ArrayView(arena_offset_t(na_off), doc.holder()).push_back(
                            AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(so)));
                    }
                    (void)root_ptr().put(la::USES.code,
                        AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(na_off)));
                }
            }
        }
    }

    // Inherit the originating user module's package name + PATH_PARTS
    // onto the synth module's MODULE root. Without this, sema_collect
    // registers emitted items under the empty package, and any non-entry
    // ast (e.g. emit_module's other stdlib files referencing the derived
    // type) fails to resolve them — find_struct_by_name iterates
    // cur_package_ + imports + reexports and never visits "". The
    // single-file pipeline tolerated empty-package because only the
    // entry ast was in play and bare-key lookups happened to fall
    // through. emit_module bundles surface this bug.
    if (g_user_root_idx < g_asts->size()) {
        auto& user_ast_pkg = (*g_asts)[g_user_root_idx];
        auto user_root_pkg = user_ast_pkg.root_object().as_tiny_map();
        auto* user_holder_pkg = user_ast_pkg.holder();
        auto* user_base_pkg = user_holder_pkg->base();
        // Always overwrite — quote_item!/parse-stub assigns its own NAME
        // (e.g. "main" or "<metaprog>") which is wrong for the user's pkg.
        if (user_root_pkg.has_key(la::NAME.code)) {
            AnyVal nm_av = user_root_pkg.get(la::NAME.code);
            if (!nm_av.is_null() && nm_av.is_pointer()) {
                auto user_name = logos::writ::StringView(
                    nm_av, user_holder_pkg).view();
                auto sv_e = doc.make_string( std::string_view(user_name));
                if (sv_e) {
                    auto sv_off = static_cast<uint32_t>(
                        sv_e->offset().value());
                    (void)root_ptr().put(la::NAME.code,
                        AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(sv_off))
                        );
                }
            }
        }
        // PATH_PARTS — array of part objects; always overwrite.
        if (user_root_pkg.has_key(la::mod::PATH_PARTS.code)) {
            AnyVal pp_av = user_root_pkg.get(la::mod::PATH_PARTS.code);
            if (!pp_av.is_null() && pp_av.is_pointer()) {
                auto user_pp = as_array(pp_av, user_holder_pkg);
                uint64_t pn = user_pp.size();
                auto a_e = doc.make_array( std::max<uint64_t>(1, pn));
                if (a_e) {
                    auto pp_off = static_cast<uint32_t>(
                        a_e->offset().value());
                    auto pp_arr_ptr = [&]() {
                        return ArrayView(arena_offset_t(pp_off), doc.holder());
                    };
                    for (uint64_t i = 0; i < pn; ++i) {
                        AnyVal part_av = user_pp.get(i);
                        if (!part_av.is_pointer()) continue;
                        const void* part_obj = part_av.resolve();
                        auto cp_e = copy_object_into(part_obj, user_base_pkg, doc);
                        if (!cp_e) continue;
                        uint32_t cp_off = static_cast<uint32_t>(
                            reinterpret_cast<uint8_t*>(cp_e.get()) - WritAccess::base(doc));
                        (void)pp_arr_ptr().push_back(
                            AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(cp_off))
                            );
                    }
                    (void)root_ptr().put(la::mod::PATH_PARTS.code,
                        AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(pp_off))
                        );
                }
            }
        }
    }

    // Inherit the originating user module's imports into the synth
    // module's USES array. Without this, derive-emitted items can only
    // reference types from the handler's own use-list — even though
    // the user clearly imported the right packages for the leaf the
    // annotation sat on. Resolves debt #13.
    if (g_user_root_idx < g_asts->size()) {
        auto& user_ast = (*g_asts)[g_user_root_idx];
        auto user_root = user_ast.root_object().as_tiny_map();
        if (user_root.has_key(la::USES.code)) {
            AnyVal user_uses_av = user_root.get(la::USES.code);
            if (!user_uses_av.is_null() && user_uses_av.is_pointer()) {
                auto* user_holder = user_ast.holder();
                auto user_uses = as_array(user_uses_av, user_holder);
                uint64_t un = user_uses.size();
                // Locate (or create) the synth module's USES array.
                AnyVal synth_uses_av = root_ptr().get(la::USES.code);
                uint32_t synth_uses_off = 0;
                if (synth_uses_av.is_null() || !synth_uses_av.is_pointer()) {
                    auto a_e = doc.make_array( std::max<uint64_t>(4, un));
                    if (!a_e) {
                        blob_seen.erase(key); return 0;
                    }
                    synth_uses_off = static_cast<uint32_t>(
                        a_e->offset().value());
                    (void)root_ptr().put(la::USES.code,
                        AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(synth_uses_off))
                        );
                } else {
                    synth_uses_off = static_cast<uint32_t>(
                        synth_uses_av.to_offset(WritAccess::base(doc)).value());
                }
                // The FULL dotted package a USE node names.
                //
                // ⚠ A `use` has TWO spellings in the AST and they are not
                // interchangeable. One the PARSER produces — head in NAME, the
                // rest in PATH_PARTS, so `use logos.mem.string;` is NAME
                // "logos" + PATH_PARTS ["mem","string"]. One THIS code produces
                // below — the whole dotted string in NAME, no PATH_PARTS. Both
                // land in the same synth USES array (a quote carries its own
                // imports, parsed; this pass appends the user's, synthesized),
                // so anything comparing them must read both fields. The dedup
                // below used to read NAME alone: every parsed import compared
                // as "logos", never matched, and got appended a second time —
                // one spurious `duplicate 'use …;' in module` warning per
                // import per emitted item, on code the user did not write.
                auto dotted_of = [](AnyVal eav, logos::writ::MemHolder* holder) -> std::string {
                    if (!eav.is_pointer()) return std::string();
                    auto node = as_tinymap(eav, holder);
                    std::string dotted;
                    if (node.has_key(la::NAME.code)) {
                        AnyVal nm_av = node.get(la::NAME.code);
                        if (!nm_av.is_null() && nm_av.is_pointer()) {
                            dotted = std::string(logos::writ::StringView(
                                nm_av, holder).view());
                        }
                    }
                    if (node.has_key(la::mod::PATH_PARTS.code)) {
                        AnyVal pp_av = node.get(la::mod::PATH_PARTS.code);
                        if (!pp_av.is_null() && pp_av.is_pointer()) {
                            auto parts = as_array(pp_av, holder);
                            for (uint64_t pi = 0; pi < parts.size(); ++pi) {
                                AnyVal pav = parts.get(pi);
                                if (!pav.is_pointer()) continue;
                                auto part = as_tinymap(pav, holder);
                                if (!part.has_key(la::NAME.code)) continue;
                                AnyVal nv = part.get(la::NAME.code);
                                if (nv.is_null() || !nv.is_pointer()) continue;
                                if (!dotted.empty()) dotted += '.';
                                dotted += std::string(logos::writ::StringView(
                                    nv, holder).view());
                            }
                        }
                    }
                    return dotted;
                };
                // Walk user's USE entries; for each, build the dotted
                // package name and dedup-append a fresh USE node into
                // the synth USES array. NAME-only form (build_import_scope
                // is happy with that).
                for (uint64_t i = 0; i < un; ++i) {
                    std::string dotted = dotted_of(user_uses.get(i), user_holder);
                    if (dotted.empty()) continue;
                    // Dedup: skip if already present in synth USES (synth
                    // already has handler's imports + own self-use baked in).
                    // ⚠ Read the synth node with the SYNTH doc's holder — it
                    // used to be read with the USER's, which is a different
                    // arena; the NAME string it then rendered was read out of
                    // the right one only by accident of the next line.
                    bool already = false;
                    auto synth_uses_ptr = ArrayView(arena_offset_t(synth_uses_off), doc.holder());
                    for (uint64_t si = 0; si < synth_uses_ptr.size(); ++si) {
                        if (dotted_of(synth_uses_ptr.get(si), doc.holder()) == dotted) {
                            already = true; break;
                        }
                    }
                    if (already) continue;
                    // Allocate name string + USE TOM.
                    auto pname_e = doc.make_string( std::string_view(dotted));
                    if (!pname_e) { blob_seen.erase(key); return 0; }
                    uint32_t pname_off = static_cast<uint32_t>(
                        pname_e->offset().value());
                    auto utom_e = doc.make_tiny_map_view( 4);
                    if (!utom_e) { blob_seen.erase(key); return 0; }
                    uint32_t utom_off = static_cast<uint32_t>(
                        utom_e->offset().value());
                    auto utom_ptr = [&]() {
                        return TinyMapView(arena_offset_t(utom_off), doc.holder());
                    };
                    (void)utom_ptr().put(la::CODE.code,
                        AnyVal::from_value(static_cast<int32_t>(la::USE.code))
                        );
                    (void)utom_ptr().put(la::NAME.code,
                        AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(pname_off))
                        );
                    auto sa = ArrayView(arena_offset_t(synth_uses_off), doc.holder());
                    (void)sa.push_back(
                        AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(utom_off))
                        );
                }
            }
        }
    }

    // Final USES dedup — the synth module's import list is assembled from
    // THREE independent sources and none of them can see the others: the
    // quote's own `use` decls, the enclosing handler module's imports baked
    // into the blob at lowering time, and the originating user module's
    // imports merged above. An emitter that names `logos.mem.string` in its
    // quote has no way to know the handler already imports it, and should not
    // have to: an import list is a SET.
    //
    // ⚠ Without this the module carries the intersection twice and
    // sema_collect warns once per duplicate per emitted item — a warning
    // written for hand-written copy-paste, fired at generated code the user
    // cannot edit. Deem's move to quotes took that from 103 to 819 per full
    // build, which is how a diagnostic channel becomes noise.
    //
    // ⚠ AND THE DUMP HID IT: `render_module_source_for_dump` prints each
    // package once, so `--gen-dir` / `--dump-metaprog` output looked clean
    // while sema saw the duplicates. Read the ARRAY, not the render.
    {
        AnyVal duav = root_ptr().get(la::USES.code);
        if (!duav.is_null() && duav.is_pointer()) {
            uint32_t duoff = static_cast<uint32_t>(
                duav.to_offset(WritAccess::base(doc)).value());
            // A use's dotted package: NAME plus PATH_PARTS. Both spellings
            // occur here — the parser emits head-in-NAME + rest-in-PATH_PARTS,
            // the merge above emits the whole dotted string in NAME — so a
            // comparison that reads NAME alone sees "logos" for every parsed
            // import and matches nothing.
            auto dotted_at = [&](AnyVal eav) -> std::string {
                if (!eav.is_pointer()) return std::string();
                auto node = as_tinymap(eav, doc.holder());
                std::string d;
                if (node.has_key(la::NAME.code)) {
                    AnyVal nm = node.get(la::NAME.code);
                    if (!nm.is_null() && nm.is_pointer())
                        d = std::string(
                            logos::writ::StringView(nm, doc.holder()).view());
                }
                if (node.has_key(la::mod::PATH_PARTS.code)) {
                    AnyVal pp = node.get(la::mod::PATH_PARTS.code);
                    if (!pp.is_null() && pp.is_pointer()) {
                        auto parts = as_array(pp, doc.holder());
                        for (uint64_t pi = 0; pi < parts.size(); ++pi) {
                            AnyVal pav = parts.get(pi);
                            if (!pav.is_pointer()) continue;
                            auto part = as_tinymap(pav, doc.holder());
                            if (!part.has_key(la::NAME.code)) continue;
                            AnyVal nv = part.get(la::NAME.code);
                            if (nv.is_null() || !nv.is_pointer()) continue;
                            if (!d.empty()) d += '.';
                            d += std::string(
                                logos::writ::StringView(nv, doc.holder()).view());
                        }
                    }
                }
                return d;
            };
            uint64_t dn = ArrayView(arena_offset_t(duoff), doc.holder()).size();
            std::vector<uint32_t> keep;
            std::set<std::string> seen_pkgs;
            bool any_dup = false;
            for (uint64_t i = 0; i < dn; ++i) {
                AnyVal e = ArrayView(arena_offset_t(duoff), doc.holder()).get(i);
                if (e.is_null() || !e.is_pointer()) continue;
                uint32_t eoff = static_cast<uint32_t>(
                    e.to_offset(WritAccess::base(doc)).value());
                std::string d = dotted_at(e);
                // An entry with no readable package (alias/wildcard forms that
                // carry their meaning elsewhere) is kept verbatim — dedup only
                // what it can NAME.
                if (!d.empty()) {
                    if (!seen_pkgs.insert(d).second) { any_dup = true; continue; }
                }
                keep.push_back(eoff);
            }
            if (any_dup) {
                auto na_e = doc.make_array(std::max<uint64_t>(1, keep.size()));
                if (na_e) {
                    uint32_t na_off = static_cast<uint32_t>(na_e->offset().value());
                    for (uint32_t so : keep) {
                        (void)ArrayView(arena_offset_t(na_off), doc.holder()).push_back(
                            AnyVal::from_offset(WritAccess::base(doc),
                                                arena_offset_t(so)));
                    }
                    (void)root_ptr().put(la::USES.code,
                        AnyVal::from_offset(WritAccess::base(doc),
                                            arena_offset_t(na_off)));
                }
            }
        }
    }

    if (WritAccess::base(doc) != doc_base_before) {
        std::fprintf(stderr,
            "logos_emit_item_blob_subst: the substitution arena moved during the "
            "walk — the pre-reserve bound was too small, so every raw pointer the "
            "walk held was freed and the emitted AST would be corrupt (a quote "
            "whose spliced fragments are large; see the reserve bound above)\n");
        blob_seen.erase(key);
        return 0;
    }

    // --gen-dir: dump the synth module as a real .logos file and swap the
    // doc for the dump's reparse (filename + SRC_LINEs then agree with the
    // file a human/debugger reads). Falls back to the in-memory doc.
    std::string synth_name = "<metaprog-blob-subst>";
    if (!g_gen_dir.empty()) {
        if (auto swapped = try_gen_dump(doc)) {
            doc        = std::move(swapped->first);
            synth_name = std::move(swapped->second);
        }
    }
    g_asts->push_back(std::move(doc));
    g_filenames->emplace_back(std::move(synth_name));
    g_from_binary->push_back(false);
    if (g_module_ids) g_module_ids->push_back(g_self_module_id);
    *g_any_emitted = true;
    record_emit_provenance();
    return 1;
}

// Function-style macros (slice 1.3b): host shim returning the bytes
// pointer for the (site_id, arg_idx)-th ARG of a `name!(...)` call.
// The driver populates `g_macro_args` before invoking each metacall
// thunk; the thunk constructs `ExprBlob { ptr: logos_macro_arg(N, i) }`
// which the splice path then decodes via lower_writ_blob.
//
// Returns ptr past the 8-byte `[u64 size]` prefix so the result is
// directly usable as an `ExprBlob.ptr` / `WritStatic.ptr`. Aborts on
// out-of-range indices — that would indicate a sema/driver mismatch.
extern "C" const uint8_t* logos_macro_arg(uint64_t site_id, uint64_t arg_idx) {
    if (!g_macro_args) {
        std::fprintf(stderr,
            "logos_macro_arg: invoked outside metacall driver (site=%llu)\n",
            static_cast<unsigned long long>(site_id));
        std::abort();
    }
    const uint8_t* p = logos::compiler::lir_mirror_macro_arg_get(*g_macro_args, site_id, arg_idx);
    if (!p) {
        std::fprintf(stderr,
            "logos_macro_arg: no arg at site %llu idx %llu\n",
            static_cast<unsigned long long>(site_id),
            static_cast<unsigned long long>(arg_idx));
        std::abort();
    }
    return p;
}

// The RULE-IR handoff. Where `logos_macro_arg` returns a byte range that the
// thunk turns into a `str`, this returns a pointer INTO the compiler's own Writ
// arena — the root of an RQProgram the compiler already parsed. The handler
// wraps it as `WAny::ref_to(ptr)` and walks it in place; nothing is copied and
// nothing is deserialised. Same shape as `logos_get_module_ast_oview`.
//
// The doc is a never-move MultiChunk arena kept alive by rule_ir's table for the
// whole dispatch loop, so the pointer cannot dangle under the handler.
extern "C" const uint8_t* logos_rule_ir(uint64_t site_id) {
    const uint8_t* p = logos::compiler::rule_ir::root_ptr(site_id);
    if (!p) {
        std::fprintf(stderr, "logos_rule_ir: no parsed rule IR at site %llu\n",
                     static_cast<unsigned long long>(site_id));
        std::abort();
    }
    return p;
}

// Pack a stack-local `[*const Ident; N]` (one entry per `#name` site
// inside `quote_item!`) into a single owned heap allocation suitable for
// storing in a QuoteItemBlob that will be returned/moved across stack
// frames. Layout:
//   [u64 N] [N × IdentPod{ptr,len}] [packed bytes...]
// IdentPod.ptr is absolute and points into the same blob's byte section.
// `logos_emit_item_blob_subst` reads this layout directly. Returns null
// if alloc fails. Even N=0 returns a non-null 8-byte header so callers
// can treat the field uniformly.
extern "C" const uint8_t* logos_qib_pack_idents(
        const void* const* arr, uint64_t n) {
    struct IdentPod { const uint8_t* ptr; uint64_t len; };
    uint64_t pod_bytes = 8 + n * sizeof(IdentPod);
    uint64_t total_str = 0;
    for (uint64_t i = 0; i < n; ++i) {
        const auto* p = reinterpret_cast<const IdentPod*>(arr[i]);
        if (p) total_str += p->len;
    }
    uint64_t total = pod_bytes + total_str;
    auto* buf = static_cast<uint8_t*>(std::malloc(total));
    if (!buf) return nullptr;
    *reinterpret_cast<uint64_t*>(buf) = n;
    auto* pods = reinterpret_cast<IdentPod*>(buf + 8);
    uint64_t byte_off = pod_bytes;
    for (uint64_t i = 0; i < n; ++i) {
        const auto* p = reinterpret_cast<const IdentPod*>(arr[i]);
        uint64_t l = (p ? p->len : 0);
        pods[i].len = l;
        pods[i].ptr = buf + byte_off;
        if (p && l > 0) {
            std::memcpy(buf + byte_off, p->ptr, l);
            byte_off += l;
        }
    }
    return buf;
}

extern "C" void logos_qib_free_idents(const uint8_t* blob) {
    if (blob) std::free(const_cast<uint8_t*>(blob));
}

// Pack a stack-local `[*const u8; N]` (one entry per `#(expr)` ExprBlob site
// inside `quote_item!`) into a single owned heap allocation. Each input
// pointer is an `ExprBlob.ptr` value: it points past an 8-byte length
// prefix into the Writ bytes. We snapshot bytes (size = *(p-8)) so the
// resulting blob owns its lifetime. Layout:
//   [u64 N] [N × BlobEntry{offset:u64, size:u64}] [concatenated bodies]
// `offset` is absolute-from-buffer-start; the splice shim reads
// `(buf+offset, size)` directly. Even N=0 returns a non-null 8-byte header.
extern "C" const uint8_t* logos_qib_pack_blobs(
        const uint8_t* const* arr, uint64_t n) {
    struct BlobEntry { uint64_t offset; uint64_t size; };
    uint64_t hdr_bytes = 8 + n * sizeof(BlobEntry);
    uint64_t total_body = 0;
    std::vector<uint64_t> sizes(n, 0);
    for (uint64_t i = 0; i < n; ++i) {
        const uint8_t* p = arr[i];
        if (p) {
            uint64_t sz = 0;
            std::memcpy(&sz, p - 8, 8);
            sizes[i] = sz;
            total_body += sz;
        }
    }
    uint64_t total = hdr_bytes + total_body;
    auto* buf = static_cast<uint8_t*>(std::malloc(total));
    if (!buf) return nullptr;
    *reinterpret_cast<uint64_t*>(buf) = n;
    auto* entries = reinterpret_cast<BlobEntry*>(buf + 8);
    uint64_t byte_off = hdr_bytes;
    for (uint64_t i = 0; i < n; ++i) {
        entries[i].offset = byte_off;
        entries[i].size   = sizes[i];
        if (arr[i] && sizes[i] > 0) {
            std::memcpy(buf + byte_off, arr[i], sizes[i]);
            byte_off += sizes[i];
        }
    }
    return buf;
}

extern "C" void logos_qib_free_blobs(const uint8_t* blob) {
    if (blob) std::free(const_cast<uint8_t*>(blob));
}

// Pack `[*const Vec<Ident>; N]` into an owned heap blob. Vec<Ident>
// layout in Logos: { ptr: *const Ident, len: u64, cap: u64 } — we read
// only ptr+len. Each Ident is { ptr: *const u8, len: u64 }; we snapshot
// the bytes too so the QIB owns its lifetime. Layout:
//   [u64 N_cursors]
//   [N_cursors × {count: u64, pods_offset: u64}]
//   [pods + ident bytes...]
// `pods_offset` is absolute-from-buffer-start.
// `depths[i]` is the nesting depth of cursor i: 1 → arr[i] is
// `*const Vec<Ident>`; 2 → `*const Vec<Vec<Ident>>` (ragged, T2-30). A null
// `depths` means all depth-1 (legacy callers). The packed blob keeps all
// idents contiguous at pods_offset; depth-2 cursors additionally store O
// sub-lengths at inner_counts_offset (CSR).
extern "C" const uint8_t* logos_qib_pack_cursors(
        const void* const* arr, const uint8_t* depths, uint64_t n) {
    struct IdentPod  { const uint8_t* ptr; uint64_t len; };
    struct CursorHdr { uint64_t count; uint64_t pods_offset; uint64_t inner_counts_offset; };
    struct VecPod    { const IdentPod* ptr; uint64_t len; uint64_t cap; };
    struct VecVecPod { const VecPod*  ptr; uint64_t len; uint64_t cap; };
    // Depth 3 = Vec<ExprBlob> splice cursor. ExprBlob is a bare pointer to
    // WritStatic-shaped bytes whose size sits 8 bytes BEFORE the pointer
    // (same convention logos_qib_pack_blobs reads). Packed as IdentPods
    // whose bytes are the serialized doc — the subst side tells them apart
    // by the 0x800000 bit in the placeholder encoding, not by layout.
    struct BlobRefPod  { const uint8_t* ptr; };
    struct VecBlobPod  { const BlobRefPod* ptr; uint64_t len; uint64_t cap; };
    auto depth_of = [&](uint64_t i) -> uint8_t {
        return depths ? depths[i] : 1;
    };
    auto blob_size_of = [](const BlobRefPod& b) -> uint64_t {
        if (!b.ptr) return 0;
        uint64_t sz = 0;
        std::memcpy(&sz, b.ptr - 8, 8);
        return sz;
    };

    uint64_t hdr_bytes = 8 + n * sizeof(CursorHdr);
    // First pass: per-cursor OUTER count, total pods, total inner_counts
    // entries (= Σ outer counts of depth-2 cursors), and total ident bytes.
    std::vector<uint64_t> outer_counts(n, 0);
    uint64_t total_pods = 0, total_str = 0, total_ic = 0;
    for (uint64_t i = 0; i < n; ++i) {
        if (depth_of(i) == 3) {
            const auto* v = reinterpret_cast<const VecBlobPod*>(arr[i]);
            if (!v) continue;
            outer_counts[i] = v->len;
            total_pods += v->len;
            for (uint64_t j = 0; j < v->len; ++j)
                total_str += blob_size_of(v->ptr[j]);
        } else if (depth_of(i) == 2) {
            const auto* vv = reinterpret_cast<const VecVecPod*>(arr[i]);
            if (!vv) continue;
            outer_counts[i] = vv->len;
            total_ic += vv->len;
            for (uint64_t o = 0; o < vv->len; ++o) {
                const VecPod& inner = vv->ptr[o];
                total_pods += inner.len;
                for (uint64_t k = 0; k < inner.len; ++k)
                    total_str += inner.ptr[k].len;
            }
        } else {
            const auto* v = reinterpret_cast<const VecPod*>(arr[i]);
            if (!v) continue;
            outer_counts[i] = v->len;
            total_pods += v->len;
            for (uint64_t j = 0; j < v->len; ++j) total_str += v->ptr[j].len;
        }
    }
    uint64_t ic_bytes = total_ic * sizeof(uint64_t);
    uint64_t total = hdr_bytes + ic_bytes
                   + total_pods * sizeof(IdentPod) + total_str;
    auto* buf = static_cast<uint8_t*>(std::malloc(total));
    if (!buf) return nullptr;
    *reinterpret_cast<uint64_t*>(buf) = n;
    auto* hdrs = reinterpret_cast<CursorHdr*>(buf + 8);
    uint64_t ic_off  = hdr_bytes;
    uint64_t pod_off = hdr_bytes + ic_bytes;
    uint64_t str_off = pod_off + total_pods * sizeof(IdentPod);
    auto emit_pod = [&](IdentPod* pods, uint64_t idx, const IdentPod& src) {
        pods[idx].len = src.len;
        if (src.len > 0 && src.ptr) {
            std::memcpy(buf + str_off, src.ptr, src.len);
            pods[idx].ptr = buf + str_off;
            str_off += src.len;
        } else {
            pods[idx].ptr = nullptr;
        }
    };
    for (uint64_t i = 0; i < n; ++i) {
        hdrs[i].count              = outer_counts[i];
        hdrs[i].pods_offset        = pod_off;
        hdrs[i].inner_counts_offset = 0;
        auto* pods = reinterpret_cast<IdentPod*>(buf + pod_off);
        if (depth_of(i) == 3) {
            const auto* v = reinterpret_cast<const VecBlobPod*>(arr[i]);
            if (!v) continue;
            for (uint64_t j = 0; j < v->len; ++j) {
                IdentPod src{v->ptr[j].ptr, blob_size_of(v->ptr[j])};
                emit_pod(pods, j, src);
            }
            pod_off += v->len * sizeof(IdentPod);
        } else if (depth_of(i) >= 2) {
            const auto* vv = reinterpret_cast<const VecVecPod*>(arr[i]);
            if (!vv) continue;
            hdrs[i].inner_counts_offset = ic_off;
            auto* ic = reinterpret_cast<uint64_t*>(buf + ic_off);
            uint64_t flat = 0;
            for (uint64_t o = 0; o < vv->len; ++o) {
                const VecPod& inner = vv->ptr[o];
                ic[o] = inner.len;
                for (uint64_t k = 0; k < inner.len; ++k)
                    emit_pod(pods, flat++, inner.ptr[k]);
            }
            ic_off  += vv->len * sizeof(uint64_t);
            pod_off += flat * sizeof(IdentPod);
        } else {
            const auto* v = reinterpret_cast<const VecPod*>(arr[i]);
            if (!v) continue;
            for (uint64_t j = 0; j < v->len; ++j) emit_pod(pods, j, v->ptr[j]);
            pod_off += v->len * sizeof(IdentPod);
        }
    }
    return buf;
}

extern "C" void logos_qib_free_cursors(const uint8_t* blob) {
    if (blob) std::free(const_cast<uint8_t*>(blob));
}

// Hygiene gensym: returns pointer to a fresh ident byte sequence
// `<prefix>__hyg_<N>`, with `*out_len` set to its length. Bytes live
// for the rest of the compilation (host owns them in a global vector
// of unique_ptr<string> so addresses stay stable across pushes).
// Counter is process-global; uniqueness across the whole compile.
static std::vector<std::unique_ptr<std::string>> g_gensym_buf;
static uint64_t                                  g_gensym_counter = 0;
extern "C" const uint8_t* logos_metaprog_gensym(const uint8_t* pref,
                                                uint64_t pref_len,
                                                uint64_t* out_len) {
    auto s = std::make_unique<std::string>();
    s->reserve(pref_len + 16);
    if (pref && pref_len > 0)
        s->append(reinterpret_cast<const char*>(pref), pref_len);
    s->append("__hyg_");
    s->append(std::to_string(g_gensym_counter++));
    *out_len = s->size();
    const uint8_t* p = reinterpret_cast<const uint8_t*>(s->data());
    g_gensym_buf.emplace_back(std::move(s));
    return p;
}

// writ metacall freeze: the Writ-returning thunk passes the Rc<Writ>'s
// root as a VALUE-FORM WAny word; deep-copy the reachable tree into a compact
// single-segment blob and return a malloc'd [u64 size][bytes] buffer (ptr past
// the prefix — the same wire shape as WritStatic; driver reads *(ptr-8)).
extern "C" const uint8_t* logos_metacall_freeze2(uint64_t root_word) {
    using logos::writ::AnyVal;
    // The Logos side passes the VALUE-form word (Pod tagged / ABSOLUTE pointer).
    // C++ AnyVal is the AT-REST form (self-relative Ref) — from_raw(absolute)
    // would resolve relative to the stack slot. Re-anchor Refs via set_ref.
    AnyVal root;
    if (root_word & 1) {
        root = AnyVal::from_raw(static_cast<int64_t>(root_word));   // Pod: verbatim
    } else if (root_word != 0) {
        root.set_ref(reinterpret_cast<const void*>(root_word));     // Ref: re-anchor
    }
    auto packed_r = logos::writ::compactify_root(root);
    if (!packed_r) return nullptr;
    auto& arena = packed_r->arena();
    const uint8_t* data = arena.head().data();
    uint64_t size = arena.total_used();
    auto* buf = static_cast<uint8_t*>(std::malloc(8 + size));
    if (!buf) return nullptr;
    std::memcpy(buf, &size, 8);
    std::memcpy(buf + 8, data, size);
    return buf + 8;
}

// Slice 5c+8 of metaprog-quote: substitution + repetition shim for
// `quote_expr!`. lower_quote_expr packs a wrapper Writ doc whose
// root TOM has VALUE=expr_offset and ITEMS=placeholder-offsets array.
// Each placeholder is a VAR_REF TOM with NAME_VAR holding an int idx
// into the caller-supplied IdentSpan array. Spans are { ptr, count };
// scalar Ident has count=1, cursor [Ident; M] has count=M.
//
// Steps:
//   1. from_bytes_copy the template;
//   2. recursive copy of the root expr into a builder doc, applying:
//      - VAR_REF with NAME_VAR(int idx): substitute NAME (using
//        idents[idx].ptr[0] for scalars; idents[idx].ptr[i] for the
//        i-th iteration when inside a REPEAT_GROUP);
//      - REPEAT_GROUP with OP=2 (`&&*`): expand body N times (N = the
//        cursor count for any cursor `#x` inside body, all cursors
//        share count) and combine via left-leaning BinOp("&&");
//   3. set_root_offset to the rebuilt expr;
//   4. clone (compacts);
//   5. malloc'd WritStatic-shaped buffer: [u64 size][bytes], return
//      ptr+8 to match ExprBlob ABI.
extern "C" const uint8_t* logos_quote_expr_subst(
        const uint8_t* tpl, uint64_t tpl_size,
        const void* idents_ptr, uint64_t idents_count) {
    namespace la = logos::compiler::ast;
    using logos::writ::AnyVal;
    using logos::writ::ArenaString;
    using logos::writ::WritAccess;
    using logos::writ::ObjectArray;
    using logos::writ::TinyObjectMap;
    using logos::writ::TinyMapView;
    using logos::writ::ArrayView;
    using logos::writ::MapView;
    using logos::writ::as_tinymap;
    using logos::writ::as_array;
    using logos::writ::arena_offset_t;
    using logos::writ::clone;
    using logos::writ::make_doc;

    if (!tpl || tpl_size == 0) return nullptr;

    struct IdentPod { const uint8_t* ptr; uint64_t len; };
    // SpanView reads from the Logos IdentSpan { ptr: *const Ident,
    // count: u64, kind: u64 }. Inside, `ptr` points at an inline
    // IdentPod array (1 slot for scalar, M slots for cursor). We step
    // sizeof(IdentPod) per element.
    struct SpanView {
        const IdentPod* slots;         // inline array of IdentPods
        uint64_t count;
        uint64_t kind;                 // 0=ident, 1=expr_blob
        const IdentPod* at(uint64_t i) const { return &slots[i]; }
    };
    // `[IdentSpan; N]` lays out inline as `[N x %IdentSpan]`.
    struct SpanRaw { const void* ptr; uint64_t count; uint64_t kind; };
    const auto* span_arr = reinterpret_cast<const SpanRaw*>(idents_ptr);
    auto get_span = [&](uint64_t i) -> SpanView {
        const SpanRaw* sr = &span_arr[i];
        return SpanView{
            reinterpret_cast<const IdentPod*>(sr->ptr),
            sr->count,
            sr->kind,
        };
    };

    auto src_e = logos::writ::from_bytes_copy(tpl, tpl_size);
    if (!src_e) {
        std::fprintf(stderr,
            "logos_quote_expr_subst: from_bytes_copy failed\n");
        return nullptr;
    }
    auto src_doc = std::move(src_e).get();
    const uint8_t* src_base = WritAccess::base(src_doc);

    auto wrapper_off = WritAccess::root_offset(src_doc);
    auto wrapper = logos::writ::TinyMapView(arena_offset_t(wrapper_off.value()), src_doc.holder());
    if (!wrapper.has_key(la::VALUE.code)) {
        std::fprintf(stderr,
            "logos_quote_expr_subst: malformed wrapper TOM\n");
        return nullptr;
    }
    AnyVal expr_av = wrapper.get(la::VALUE.code);
    if (!expr_av.is_pointer()) {
        std::fprintf(stderr,
            "logos_quote_expr_subst: wrapper VALUE not pointer\n");
        return nullptr;
    }
    uint32_t src_root_off =
        static_cast<uint32_t>(expr_av.to_offset(src_base).value());

    // DST is GrowableSingleChunk (one segment): the substitution below addresses it
    // by base(doc)+off; a MultiChunk doc would scatter that across chunks once the
    // expansion outgrows the first chunk. Pre-size past any realloc (lazy-zero → cheap).
    auto dst_e = make_doc(std::max<size_t>(tpl_size + 256, size_t(8) * 1024 * 1024),
                          logos::writ::ArenaMode::GrowableSingleChunk);
    if (!dst_e) return nullptr;
    auto dst_doc = std::move(dst_e).get();

    // Cursor index when inside a REPEAT_GROUP iteration (-1 outside).
    int64_t cursor_i = -1;

    auto str_for_ident = [&](const IdentPod* idp) -> uint32_t {
        if (!idp || !idp->ptr || idp->len == 0) return 0;
        auto str_e = dst_doc.make_string(
            std::string_view(reinterpret_cast<const char*>(idp->ptr),
                             idp->len));
        if (!str_e) return 0;
        return static_cast<uint32_t>(
            str_e->offset().value());
    };

    // Forward decl.
    std::function<uint32_t(uint32_t)> copy_expr;
    // Forward decl: copy a src ObjectArray into a fresh dst ObjectArray,
    // expanding REPEAT_GROUP elements with sep=0 (`*`) or sep=1 (`,*`)
    // into N substituted siblings spliced inline.
    std::function<uint32_t(uint32_t)> copy_array;

    // Generic cursor-count discovery: walk a body tree (TOM/ObjectArray/string)
    // looking for any NAME_VAR(int idx) placeholder bound to a cursor span
    // (count > 1 OR kind != 1 ExprBlob). Recurses through every TOM-typed key
    // and every ObjectArray element. Stops at the first cursor it finds.
    // Used by REPEAT_GROUP body to determine N before expansion.
    std::function<uint64_t(uint32_t)> find_cursor_count;
    find_cursor_count = [&](uint32_t off) -> uint64_t {
        auto tag0 = logos::writ::TypeTag::read_before(src_base + off);
        uint64_t tc0 = tag0.type_code();
        if (tc0 == 100) {
            auto arr = logos::writ::ArrayView(arena_offset_t(off), src_doc.holder());
            uint64_t n = arr.size();
            for (uint64_t i = 0; i < n; ++i) {
                AnyVal el = arr.get(i);
                if (el.is_null() || !el.is_pointer()) continue;
                uint64_t r = find_cursor_count(
                    static_cast<uint32_t>(el.to_offset(src_base).value()));
                if (r != static_cast<uint64_t>(-1)) return r;
            }
            return static_cast<uint64_t>(-1);
        }
        if (tc0 == 130) return static_cast<uint64_t>(-1);
        // Treat as TOM. Check for NAME_VAR(int idx) on this node.
        auto tt = logos::writ::TinyMapView(arena_offset_t(off), src_doc.holder());
        if (tt.has_key(la::NAME_VAR.code)) {
            AnyVal iv = tt.get(la::NAME_VAR.code);
            if (!iv.is_null() && !iv.is_pointer()) {
                int32_t idx = iv.as_value<int32_t>();
                if (idx >= 0
                    && static_cast<uint64_t>(idx) < idents_count) {
                    auto sp = get_span(idx);
                    // kind=1 (scalar ExprBlob) — never a cursor.
                    // kind=2 (Vec<ExprBlob>) — a cursor BY KIND: count 1 and
                    //   even 0 are legitimate fold lengths (the -1 sentinel
                    //   below distinguishes "no cursor found").
                    // kind=0 (Ident) — scalar and 1-element Vec cursors pack
                    //   identically (count=1), so count > 1 stays the only
                    //   safe discriminator.
                    if (sp.kind == 2) return sp.count;
                    if (sp.kind == 0 && sp.count > 1) return sp.count;
                }
            }
        }
        // Recurse into every TOM-typed pointer key and every array key.
        for (uint8_t k = 0; k < TinyObjectMap::MAX_KEYS; ++k) {
            if (!tt.has_key(k)) continue;
            AnyVal av = tt.get(k);
            if (av.is_null() || !av.is_pointer()) continue;
            uint32_t coff = static_cast<uint32_t>(av.to_offset(src_base).value());
            uint64_t r = find_cursor_count(coff);
            if (r != static_cast<uint64_t>(-1)) return r;
        }
        return static_cast<uint64_t>(-1);
    };

    // Step 5c Option B: deep-copy any Writ node tree (TOM + ObjectArray
    // + ArenaString) from an arbitrary source base into dst_doc, with no
    // antiquot substitution. Used to splice the body of a captured
    // ExprBlob into the outer template at a `#name` placeholder.
    std::function<uint32_t(logos::writ::MemHolder*, uint32_t)> copy_node_raw;
    copy_node_raw = [&](logos::writ::MemHolder* sh, uint32_t off) -> uint32_t {
        const uint8_t* sb = sh->base();
        auto tag = logos::writ::TypeTag::read_before(sb + off);
        uint64_t tc = tag.type_code();
        if (tc == 130) {
            auto se = dst_doc.make_string(
                logos::writ::StringView(arena_offset_t(off), sh).view());
            if (!se) return 0;
            return static_cast<uint32_t>(
                se->offset().value());
        }
        if (tc == 100) {
            auto arr = logos::writ::ArrayView(arena_offset_t(off), sh);
            uint64_t n = arr.size();
            auto dst_e = dst_doc.make_array( std::max<uint64_t>(4, n));
            if (!dst_e) return 0;
            uint32_t dst_off = static_cast<uint32_t>(
                dst_e->offset().value());
            auto dst_arr = [&]() {
                return logos::writ::ArrayView(arena_offset_t(dst_off), dst_doc.holder());
            };
            for (uint64_t i = 0; i < n; ++i) {
                AnyVal el = arr.get(i);
                if (!el.is_pointer()) {
                    (void)dst_arr().push_back(el);
                    continue;
                }
                uint32_t no = copy_node_raw(sh,
                    static_cast<uint32_t>(el.to_offset(sb).value()));
                if (no == 0) return 0;
                (void)dst_arr().push_back(
                    AnyVal::from_offset(WritAccess::base(dst_doc), arena_offset_t(no)));
            }
            return dst_off;
        }
        // TOM (tag 98 or untagged AST node).
        auto tom = logos::writ::TinyMapView(arena_offset_t(off), sh);
        uint8_t cap = static_cast<uint8_t>(tom.capacity());
        if (cap < 4) cap = 4;
        auto dst_e = dst_doc.make_tiny_map_view( cap);
        if (!dst_e) return 0;
        uint32_t dst_off = static_cast<uint32_t>(
            dst_e->offset().value());
        auto dst_tom = [&]() {
            return logos::writ::TinyMapView(arena_offset_t(dst_off), dst_doc.holder());
        };
        if (tom.schema_type_code() != 0) {
            dst_tom().set_schema_type_code(tom.schema_type_code());
        }
        for (uint8_t k = 0; k < TinyObjectMap::MAX_KEYS; ++k) {
            if (!tom.has_key(k)) continue;
            AnyVal av = tom.get(k);
            if (av.is_null() || !av.is_pointer()) {
                (void)dst_tom().put(k, av);
                continue;
            }
            uint32_t no = copy_node_raw(sh,
                static_cast<uint32_t>(av.to_offset(sb).value()));
            if (no == 0) return 0;
            (void)dst_tom().put(k,
                AnyVal::from_offset(WritAccess::base(dst_doc), arena_offset_t(no)));
        }
        return dst_off;
    };

    // Inner ExprBlob docs deserialised at splice time must outlive the
    // copy_node_raw recursion (their bytes back the src_base pointer).
    std::vector<logos::writ::Writ> inner_blob_docs;

    // Expand REPEAT_GROUP body into a left-leaning BinOp tree of "&&".
    // For N==0 returns 0 (caller treats as null). For N==1 returns the
    // single substituted body. For N>=2 builds (((b0 && b1) && b2)...).
    auto expand_andand = [&](uint32_t body_off, uint64_t n) -> uint32_t {
        if (n == 0) return 0;
        cursor_i = 0;
        uint32_t acc = copy_expr(body_off);
        for (uint64_t i = 1; i < n; ++i) {
            cursor_i = static_cast<int64_t>(i);
            uint32_t rhs = copy_expr(body_off);
            // Build BinOp { CODE: BINOP, OP: "&&", LHS: acc, RHS: rhs }.
            auto bin_e = dst_doc.make_tiny_map_view( 4);
            if (!bin_e) return 0;
            uint32_t bin_off = static_cast<uint32_t>(
                bin_e->offset().value());
            auto bin = [&]() {
                return logos::writ::TinyMapView(arena_offset_t(bin_off), dst_doc.holder());
            };
            (void)bin().put(la::CODE.code,
                AnyVal::from_value<int32_t>(la::BINOP.code));
            auto op_e = dst_doc.make_string( std::string_view("&&"));
            if (!op_e) return 0;
            uint32_t op_off = static_cast<uint32_t>(
                op_e->offset().value());
            (void)bin().put(la::OP.code,
                AnyVal::from_offset(WritAccess::base(dst_doc), arena_offset_t(op_off)));
            (void)bin().put(la::LHS.code,
                AnyVal::from_offset(WritAccess::base(dst_doc), arena_offset_t(acc)));
            (void)bin().put(la::RHS.code,
                AnyVal::from_offset(WritAccess::base(dst_doc), arena_offset_t(rhs)));
            bin().set_schema_type_code(
                logos::writ::schema::ast(la::BINOP.code));
            acc = bin_off;
        }
        cursor_i = -1;
        return acc;
    };

    // Recursive copy: src_off (in src_doc) → fresh dst offset (in dst_doc),
    // applying placeholder substitution and REPEAT_GROUP expansion.
    copy_expr = [&](uint32_t src_off) -> uint32_t {
        auto src_tom = logos::writ::TinyMapView(arena_offset_t(src_off), src_doc.holder());
        int32_t cd = 0;
        if (src_tom.has_key(la::CODE.code)) {
            AnyVal cav = src_tom.get(la::CODE.code);
            if (!cav.is_null() && !cav.is_pointer())
                cd = cav.as_value<int32_t>();
        }

        // REPEAT_GROUP: expand here. Caller is the expr edge in parent.
        if (cd == la::REPEAT_GROUP.code) {
            int32_t sep = 0;
            if (src_tom.has_key(la::OP.code)) {
                AnyVal sav = src_tom.get(la::OP.code);
                if (!sav.is_null() && !sav.is_pointer())
                    sep = sav.as_value<int32_t>();
            }
            if (sep != 2) {
                std::fprintf(stderr,
                    "logos_quote_expr_subst: REPEAT_GROUP sep %d not implemented\n",
                    sep);
                return 0;
            }
            AnyVal bav = src_tom.get(la::VALUE.code);
            if (!bav.is_pointer()) return 0;
            uint32_t body_off =
                static_cast<uint32_t>(bav.to_offset(src_base).value());
            uint64_t n = find_cursor_count(body_off);
            if (n == static_cast<uint64_t>(-1) || n == 0) {
                // an empty `&&*`-fold has no value to yield — count 0 is as
                // fatal here as a missing cursor
                std::fprintf(stderr,
                    "logos_quote_expr_subst: REPEAT_GROUP has no cursor "
                    "or an empty one in value position\n");
                return 0;
            }
            return expand_andand(body_off, n);
        }

        // 5c Option B: ExprBlob splice. Detect placeholders whose span kind=1
        // BEFORE allocating a TOM — we replace the entire node with a deep
        // copy of the blob's root.
        // Slice 1.6: kind=2 is the Vec<ExprBlob> cursor flavor — slots
        // is a contiguous *const u8 array (8-byte stride), and we pick
        // the cursor_i-th element per `#(...)*` iteration.
        //
        // ⚠ Gated on the SPAN KIND, not on the node's CODE. It used to demand
        // VAR_REF, which silently excluded every antiquote the grammar puts on
        // some other node — `#(ty)` in a type slot parses as TYPE_REF{NAME_VAR}
        // — so a fragment spliced into a type fell through to the Ident path,
        // read the blob pointer as an IdentPod and produced an EMPTY blob with
        // no diagnostic at all. A kind-0 (Ident) span still falls through here,
        // which is what keeps `#name` working on any node.
        if (src_tom.has_key(la::NAME_VAR.code)) {
            AnyVal iv0 = src_tom.get(la::NAME_VAR.code);
            if (!iv0.is_null() && !iv0.is_pointer()) {
                int32_t idx0 = iv0.as_value<int32_t>();
                if (idx0 >= 0
                    && static_cast<uint64_t>(idx0) < idents_count) {
                    SpanView sp0 = get_span(idx0);
                    if (sp0.kind == 1 || sp0.kind == 2) {
                        const uint8_t* blob_data = nullptr;
                        if (sp0.kind == 1) {
                            blob_data = reinterpret_cast<const uint8_t*>(sp0.slots);
                        } else {
                            uint64_t i = (sp0.count == 1) ? 0
                                : (cursor_i >= 0
                                   ? static_cast<uint64_t>(cursor_i) : 0);
                            if (i >= sp0.count) {
                                std::fprintf(stderr,
                                    "logos_quote_expr_subst: ExprBlob cursor "
                                    "i=%llu out of range (count=%llu)\n",
                                    (unsigned long long)i,
                                    (unsigned long long)sp0.count);
                                return 0;
                            }
                            const uint8_t* const* arr =
                                reinterpret_cast<const uint8_t* const*>(sp0.slots);
                            blob_data = arr[i];
                        }
                        if (!blob_data) {
                            std::fprintf(stderr,
                                "logos_quote_expr_subst: ExprBlob splice — null ptr\n");
                            return 0;
                        }
                        uint64_t blob_size = 0;
                        std::memcpy(&blob_size, blob_data - 8, 8);
                        auto inner_e = logos::writ::from_bytes_copy(
                            blob_data, blob_size);
                        if (!inner_e) {
                            std::fprintf(stderr,
                                "logos_quote_expr_subst: ExprBlob splice — from_bytes_copy failed\n");
                            return 0;
                        }
                        inner_blob_docs.push_back(std::move(inner_e).get());
                        auto& inner_doc = inner_blob_docs.back();
                        uint32_t inner_root = static_cast<uint32_t>(
                            WritAccess::root_offset(inner_doc).value());
                        return copy_node_raw(inner_doc.holder(), inner_root);
                    }
                }
            }
        }

        // Allocate a fresh dst TOM for this node; copy keys.
        uint8_t cap = src_tom.capacity();
        if (cap < 4) cap = 4;
        auto dst_e = dst_doc.make_tiny_map_view( cap);
        if (!dst_e) return 0;
        uint32_t dst_off = static_cast<uint32_t>(
            dst_e->offset().value());
        auto dst_tom = [&]() {
            return logos::writ::TinyMapView(arena_offset_t(dst_off), dst_doc.holder());
        };
        if (src_tom.schema_type_code() != 0) {
            dst_tom().set_schema_type_code(src_tom.schema_type_code());
        }

        // VAR_REF with NAME_VAR(int idx) → emit NAME(string_from_ident).
        if (cd == la::VAR_REF.code && src_tom.has_key(la::NAME_VAR.code)) {
            AnyVal iv = src_tom.get(la::NAME_VAR.code);
            if (iv.is_null() || iv.is_pointer()) {
                std::fprintf(stderr,
                    "logos_quote_expr_subst: NAME_VAR not int idx\n");
                return 0;
            }
            int32_t idx = iv.as_value<int32_t>();
            if (idx < 0
                || static_cast<uint64_t>(idx) >= idents_count) {
                std::fprintf(stderr,
                    "logos_quote_expr_subst: ident idx %d out of range\n",
                    idx);
                return 0;
            }
            SpanView sp = get_span(idx);
            uint64_t i = (sp.count == 1) ? 0
                : (cursor_i >= 0 ? static_cast<uint64_t>(cursor_i) : 0);
            if (i >= sp.count) return 0;
            const IdentPod* idp = sp.at(i);
            uint32_t name_off = str_for_ident(idp);
            if (name_off == 0) return 0;
            (void)dst_tom().put(la::CODE.code,
                AnyVal::from_value<int32_t>(la::VAR_REF.code));
            (void)dst_tom().put(la::NAME.code,
                AnyVal::from_offset(WritAccess::base(dst_doc), arena_offset_t(name_off)));
            // Copy SRC_LINE if present so error messages keep line info.
            if (src_tom.has_key(la::SRC_LINE.code)) {
                AnyVal lav = src_tom.get(la::SRC_LINE.code);
                if (!lav.is_null() && !lav.is_pointer()) {
                    (void)dst_tom().put(la::SRC_LINE.code, lav);
                }
            }
            return dst_off;
        }

        // Generic: copy each key. We dispatch by the child's TypeTag
        // (written as the byte immediately preceding the object) so the
        // shim handles arbitrary AST shapes uniformly:
        //   - 28  (WritString): deep-copy bytes via ArenaString::create
        //   - 100 (ObjectArray):  copy_array (handles REPEAT_GROUP splice)
        //   - else (98 TOM or untagged AST node): recurse copy_expr
        for (uint8_t k = 0; k < TinyObjectMap::MAX_KEYS; ++k) {
            if (!src_tom.has_key(k)) continue;
            AnyVal av = src_tom.get(k);
            // NAME_VAR(int idx) on any non-VAR_REF node (e.g. FIELD_INIT
            // for `#(field):val` antiquot) → resolve and emit NAME(string).
            // VAR_REF is short-circuited above; here we cover FIELD_INIT and
            // any future node that adopts the same antiquot convention.
            if (k == la::NAME_VAR.code && !av.is_null() && !av.is_pointer()) {
                int32_t idx = av.as_value<int32_t>();
                if (idx < 0
                    || static_cast<uint64_t>(idx) >= idents_count) {
                    std::fprintf(stderr,
                        "logos_quote_expr_subst: NAME_VAR idx %d out of range\n",
                        idx);
                    return 0;
                }
                SpanView sp = get_span(idx);
                uint64_t i = (sp.count == 1) ? 0
                    : (cursor_i >= 0 ? static_cast<uint64_t>(cursor_i) : 0);
                if (i >= sp.count) return 0;
                uint32_t name_off = str_for_ident(sp.at(i));
                if (name_off == 0) return 0;
                // Where the substituted text LANDS depends on the node, and the
                // slot is not NAME on every one of them:
                //   • FIELD_READ  → FIELD    (`x.#f`)
                //   • LIT_STR     → VALUE    (`##name` — an ident AS a string
                //                             literal; with NAME it produced an
                //                             EMPTY string and nothing said so)
                //   • STATIC_CALL → RECEIVER (`#T::method(args)` — NAME already
                //                             holds the literal method name)
                //   • everything else (VAR_REF, FIELD_INIT, LET, …) → NAME.
                // ⚠ Kept in step with the item shim's identical table in
                // logos_emit_item_blob_subst; the two quote flavours must land
                // the same ident in the same slot.
                uint8_t out_key = la::NAME.code;
                if (cd == la::FIELD_READ.code)        out_key = la::FIELD.code;
                else if (cd == la::LIT_STR.code)      out_key = la::VALUE.code;
                else if (cd == la::STATIC_CALL.code)  out_key = la::RECEIVER.code;
                (void)dst_tom().put(out_key,
                    AnyVal::from_offset(WritAccess::base(dst_doc), arena_offset_t(name_off)));
                continue;
            }
            if (av.is_null()) {
                (void)dst_tom().put(k, av);
                continue;
            }
            if (!av.is_pointer()) {
                (void)dst_tom().put(k, av);
                continue;
            }
            uint32_t child_off =
                static_cast<uint32_t>(av.to_offset(src_base).value());
            auto tag = logos::writ::TypeTag::read_before(
                src_base + child_off);
            uint64_t tc = tag.type_code();

            if (tc == 130) {
                auto se = dst_doc.make_string(logos::writ::StringView(arena_offset_t(child_off), src_doc.holder()).view());
                if (!se) return 0;
                uint32_t s_off = static_cast<uint32_t>(
                    se->offset().value());
                (void)dst_tom().put(k,
                    AnyVal::from_offset(WritAccess::base(dst_doc), arena_offset_t(s_off)));
            } else if (tc == 100) {
                uint32_t new_off = copy_array(child_off);
                if (new_off == 0) return 0;
                (void)dst_tom().put(k,
                    AnyVal::from_offset(WritAccess::base(dst_doc), arena_offset_t(new_off)));
            } else {
                uint32_t new_off = copy_expr(child_off);
                if (new_off == 0) return 0;
                (void)dst_tom().put(k,
                    AnyVal::from_offset(WritAccess::base(dst_doc), arena_offset_t(new_off)));
            }
        }
        return dst_off;
    };

    // ObjectArray copy with REPEAT_GROUP splice. Elements that are
    // REPEAT_GROUP TOMs with sep=0 (`*`) or sep=1 (`,*`) expand into N
    // substituted bodies spliced inline as siblings; sep=2 (`&&*`) collapses
    // to a single BinOp tree (handled by copy_expr).
    // Statement-list INLINE splice (ADR 0024 S5).
    //
    // `#frag;` at statement position parses as EXPR_STMT{VALUE: VAR_REF#frag}.
    // When the blob behind the placeholder is BLOCK-rooted — what `quote_block!`
    // and `parse_block` produce — its statements belong to the array being
    // copied, NOT to a nested block: an emitter composes a body out of
    // runtime-many statement RUNS, and a run that opens its own scope cannot
    // export the bindings the run after it reads (`let __rel_X` in a prelude,
    // `let mut __hm{k}` in a join build phase).
    //
    // ⚠ Scoping is not lost, it is spelled: `{ #frag; }` still nests, because
    // the braces are then the template's own BLOCK. The inline is the primitive
    // and the scope is the wrapper, which is the way round that composes.
    //
    // A non-BLOCK blob (an expression fragment) is untouched — it stays an
    // expression statement. Returns 1 = inlined, 0 = not applicable (caller
    // copies the element normally), -1 = hard error.
    //
    // ⚠ `copy_node_raw` never pushes to `inner_blob_docs`, so the reference to
    // the doc we just deserialised stays valid across the whole item loop; a
    // copier that DID push would have to re-resolve it every turn.
    auto try_inline_stmt_block =
        [&](uint32_t el_off, const std::function<bool(uint32_t)>& push) -> int {
        auto code_of_node = [&](uint32_t off) -> int32_t {
            auto t = logos::writ::TinyMapView(arena_offset_t(off), src_doc.holder());
            if (!t.has_key(la::CODE.code)) return 0;
            AnyVal cav = t.get(la::CODE.code);
            if (cav.is_null() || cav.is_pointer()) return 0;
            return cav.as_value<int32_t>();
        };
        if (code_of_node(el_off) != la::EXPR_STMT.code) return 0;
        auto el_tom = logos::writ::TinyMapView(arena_offset_t(el_off), src_doc.holder());
        if (!el_tom.has_key(la::VALUE.code)) return 0;
        AnyVal vav = el_tom.get(la::VALUE.code);
        if (vav.is_null() || !vav.is_pointer()) return 0;
        uint32_t v_off = static_cast<uint32_t>(vav.to_offset(src_base).value());
        if (code_of_node(v_off) != la::VAR_REF.code) return 0;
        auto v_tom = logos::writ::TinyMapView(arena_offset_t(v_off), src_doc.holder());
        if (!v_tom.has_key(la::NAME_VAR.code)) return 0;
        AnyVal iv = v_tom.get(la::NAME_VAR.code);
        if (iv.is_null() || iv.is_pointer()) return 0;
        int32_t idx = iv.as_value<int32_t>();
        if (idx < 0 || static_cast<uint64_t>(idx) >= idents_count) return 0;
        SpanView sp = get_span(idx);
        const uint8_t* blob_data = nullptr;
        if (sp.kind == 1) {
            blob_data = reinterpret_cast<const uint8_t*>(sp.slots);
        } else if (sp.kind == 2) {
            uint64_t i = (sp.count == 1) ? 0
                : (cursor_i >= 0 ? static_cast<uint64_t>(cursor_i) : 0);
            if (i >= sp.count) {
                std::fprintf(stderr,
                    "logos_quote_expr_subst: stmt-list splice — cursor i=%llu "
                    "out of range (count=%llu)\n",
                    (unsigned long long)i, (unsigned long long)sp.count);
                return -1;
            }
            blob_data = reinterpret_cast<const uint8_t* const*>(sp.slots)[i];
        } else {
            return 0;   // an Ident placeholder — not a fragment at all
        }
        if (!blob_data) {
            std::fprintf(stderr,
                "logos_quote_expr_subst: stmt-list splice — null ptr\n");
            return -1;
        }
        uint64_t blob_size = 0;
        std::memcpy(&blob_size, blob_data - 8, 8);
        auto inner_e = logos::writ::from_bytes_copy(blob_data, blob_size);
        if (!inner_e) {
            std::fprintf(stderr,
                "logos_quote_expr_subst: stmt-list splice — from_bytes_copy failed\n");
            return -1;
        }
        inner_blob_docs.push_back(std::move(inner_e).get());
        auto& inner_doc = inner_blob_docs.back();
        auto* ih = inner_doc.holder();
        uint32_t inner_root = static_cast<uint32_t>(
            WritAccess::root_offset(inner_doc).value());
        auto rt = logos::writ::TinyMapView(arena_offset_t(inner_root), ih);
        int32_t rcd = 0;
        if (rt.has_key(la::CODE.code)) {
            AnyVal cav = rt.get(la::CODE.code);
            if (!cav.is_null() && !cav.is_pointer()) rcd = cav.as_value<int32_t>();
        }
        if (rcd != la::BLOCK.code) {
            // Not a statement list — hand it back to the ordinary blob path,
            // which splices the root expression in place of the placeholder.
            inner_blob_docs.pop_back();
            return 0;
        }
        if (!rt.has_key(la::ITEMS.code)) return 1;   // `{}` — splice nothing
        AnyVal iav = rt.get(la::ITEMS.code);
        if (iav.is_null() || !iav.is_pointer()) return 1;
        uint32_t items_off = static_cast<uint32_t>(iav.to_offset(ih->base()).value());
        auto items = logos::writ::ArrayView(arena_offset_t(items_off), ih);
        uint64_t n_items = items.size();
        for (uint64_t j = 0; j < n_items; ++j) {
            AnyVal e = items.get(j);
            if (e.is_null() || !e.is_pointer()) continue;
            uint32_t so = static_cast<uint32_t>(e.to_offset(ih->base()).value());
            uint32_t no = copy_node_raw(ih, so);
            if (no == 0) return -1;
            if (!push(no)) return -1;
        }
        return 1;
    };

    copy_array = [&](uint32_t src_off) -> uint32_t {
        auto src_arr = logos::writ::ArrayView(arena_offset_t(src_off), src_doc.holder());
        uint64_t n_src = src_arr.size();
        auto dst_e = dst_doc.make_array( std::max<uint64_t>(4, n_src));
        if (!dst_e) return 0;
        uint32_t dst_off = static_cast<uint32_t>(
            dst_e->offset().value());
        auto dst_arr = [&]() {
            return logos::writ::ArrayView(arena_offset_t(dst_off), dst_doc.holder());
        };
        std::function<bool(uint32_t)> push_off = [&](uint32_t o) -> bool {
            (void)dst_arr().push_back(
                AnyVal::from_offset(WritAccess::base(dst_doc), arena_offset_t(o)));
            return true;
        };
        for (uint64_t i = 0; i < n_src; ++i) {
            AnyVal el = src_arr.get(i);
            if (!el.is_pointer()) {
                (void)dst_arr().push_back(el);
                continue;
            }
            uint32_t el_off =
                static_cast<uint32_t>(el.to_offset(src_base).value());
            auto el_tom = logos::writ::TinyMapView(arena_offset_t(el_off), src_doc.holder());
            int32_t el_cd = 0;
            if (el_tom.has_key(la::CODE.code)) {
                AnyVal cav = el_tom.get(la::CODE.code);
                if (!cav.is_null() && !cav.is_pointer())
                    el_cd = cav.as_value<int32_t>();
            }
            int32_t sep = -1;
            if (el_cd == la::REPEAT_GROUP.code
                && el_tom.has_key(la::OP.code)) {
                AnyVal sav = el_tom.get(la::OP.code);
                if (!sav.is_null() && !sav.is_pointer())
                    sep = sav.as_value<int32_t>();
            }
            if (el_cd == la::REPEAT_GROUP.code
                && (sep == 0 || sep == 1)) {
                // Splice: expand body N times, push each as sibling element.
                AnyVal bav = el_tom.get(la::VALUE.code);
                if (!bav.is_pointer()) return 0;
                uint32_t body_off =
                    static_cast<uint32_t>(bav.to_offset(src_base).value());
                uint64_t n = find_cursor_count(body_off);
                if (n == static_cast<uint64_t>(-1)) {
                    std::fprintf(stderr,
                        "logos_quote_expr_subst: REPEAT_GROUP body has no cursor\n");
                    return 0;
                }
                // n == 0: a legitimately EMPTY fold — splice nothing.
                int64_t saved = cursor_i;
                for (uint64_t j = 0; j < n; ++j) {
                    cursor_i = static_cast<int64_t>(j);
                    // `#( #frags; )*` over a Vec<ExprBlob> of statement lists:
                    // each turn contributes its RUN of statements, flat.
                    int inl = try_inline_stmt_block(body_off, push_off);
                    if (inl < 0) { cursor_i = saved; return 0; }
                    if (inl == 1) continue;
                    uint32_t copy_off = copy_expr(body_off);
                    if (copy_off == 0) {
                        cursor_i = saved;
                        return 0;
                    }
                    (void)dst_arr().push_back(
                        AnyVal::from_offset(WritAccess::base(dst_doc), arena_offset_t(copy_off)));
                }
                cursor_i = saved;
            } else {
                int inl = try_inline_stmt_block(el_off, push_off);
                if (inl < 0) return 0;
                if (inl == 1) continue;
                uint32_t copy_off = copy_expr(el_off);
                if (copy_off == 0) return 0;
                (void)dst_arr().push_back(
                    AnyVal::from_offset(WritAccess::base(dst_doc), arena_offset_t(copy_off)));
            }
        }
        return dst_off;
    };

    uint32_t new_root = copy_expr(src_root_off);
    if (new_root == 0) return nullptr;
    WritAccess::set_root_offset(dst_doc, arena_offset_t(new_root));

    auto packed_e = compactify(dst_doc);
    if (!packed_e) {
        std::fprintf(stderr,
            "logos_quote_expr_subst: clone failed\n");
        return nullptr;
    }
    auto packed = std::move(packed_e).get();
    auto& packed_arena = WritAccess::arena(packed);
    const uint8_t* data = packed_arena.head().data();
    size_t used = packed_arena.total_used();

    uint8_t* buf = static_cast<uint8_t*>(std::malloc(8 + used));
    if (!buf) return nullptr;
    uint64_t sz = used;
    std::memcpy(buf, &sz, 8);
    std::memcpy(buf + 8, data, used);
    return buf + 8;
}

// parse_as — reify a runtime STRING into a spliceable AST fragment. Parses `s`
// as one of a small curated set of exported grammar nonterminals (rule_id) and
// returns an ExprBlob-ABI buffer: `[u64 size][serialized fragment-doc bytes]`,
// pointer returned PAST the 8-byte size prefix (identical ABI to
// logos_quote_expr_subst). The fragment splices into a quote_item! template at
// the matching position through the EXISTING `#(exprblob)` path — try_blob_splice
// copies the subtree verbatim and is agnostic to node kind, so a `type_param_list`
// or `type_ref` node drops straight into the generics / receiver-type slot.
//
// This is the general "runtime string → AST" primitive that lets emitters keep
// building fragments as strings (their strength) while splicing them HYGIENICALLY
// into a structured quote template — instead of raw push_text for the whole item.
//
//   rule_id 0 = type_param_list  ("<T: Ord + CanonCol>")
//   rule_id 1 = type_ref         ("VecCtr<T>", "i64", "&[u8]")
//
// The WHOLE string must parse and be consumed (trailing input → error → null).
// Buffer is malloc'd; ownership transfers to the caller (freed via
// logos_qib_free_blobs after splice, like any runtime ExprBlob).
extern "C" const uint8_t* logos_parse_as(const uint8_t* s, uint64_t len,
                                         uint32_t rule_id) {
    namespace la = logos::compiler::ast;
    using logos::writ::WritAccess;
    using logos::writ::arena_offset_t;
    using logos::writ::AnyVal;
    using logos::writ::TinyMapView;
    logos::writ::Writ doc;

    // rule 0 (type_param_list) with empty / whitespace-only input yields an
    // EMPTY list `{ITEMS: []}` — a concrete container's absent generics. Spliced
    // at an impl/fn header it renders no `<...>`, so a from-string emitter needs
    // no separate no-generics variant (and the antiquot-fn grammar, which always
    // expects a type_param_list, stays satisfied).
    bool empty_tpl = (rule_id == 0);
    for (uint64_t i = 0; empty_tpl && i < len; ++i)
        if (s && s[i] != ' ' && s[i] != '\t') empty_tpl = false;

    if (empty_tpl) {
        auto de = logos::writ::make_doc(4096);
        if (!de) return nullptr;
        doc = std::move(de).get();
        auto ae = doc.make_array(1);
        if (!ae) return nullptr;
        uint32_t aoff = static_cast<uint32_t>(ae->offset().value());
        auto te = doc.make_tiny_map_view(2);
        if (!te) return nullptr;
        uint32_t toff = static_cast<uint32_t>(te->offset().value());
        (void)TinyMapView(arena_offset_t(toff), doc.holder()).put(
            la::ITEMS.code,
            AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(aoff)));
        doc.set_root(AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(toff)));
    } else {
        if (!s || len == 0) return nullptr;
        logos::compiler::LogosParser parser(
            std::string_view(reinterpret_cast<const char*>(s), len));
        switch (rule_id) {
            case 0: doc = parser.parse_type_param_list(); break;
            case 1: doc = parser.parse_type_ref();        break;
            case 2: doc = parser.parse_expr();            break;
            case 3: doc = parser.parse_wstatic_lit_type(); break;
            // rule 4 (block) — a BRACED STATEMENT LIST. This is the rule the
            // emitters need: they build a loop nest whose DEPTH is a runtime
            // value (a join chain of N steps), which no fixed quote template can
            // express and no `#( … )*` repeat can either, since a repeat
            // produces a flat sequence and a nest is not flat. Reifying the
            // built text as a block lets the emitter keep doing what it is good
            // at — assembling the body — while the ITEM around it becomes a real
            // quote instead of raw push_text (ADR 0024 S5).
            case 4: doc = parser.parse_block();           break;
            // rule 5 (param_list) — `a: &Foo, b: i64`. The other half of what a
            // quoted emitted FN needs: an emitter builds its signature's
            // parameters as text, verbatim from the query's own param list.
            case 5: doc = parser.parse_param_list();      break;
            default:
                std::fprintf(stderr, "logos_parse_as: unknown rule_id %u\n", rule_id);
                return nullptr;
        }
        if (doc.is_null()) {
            std::fprintf(stderr, "logos_parse_as: parse failed (rule %u)\n", rule_id);
            return nullptr;
        }
        if (!parser.at_eof()) {
            std::fprintf(stderr,
                "logos_parse_as: trailing input after rule %u (near line %u)\n",
                rule_id, parser.next_line());
            return nullptr;
        }
    }
    auto packed_e = compactify(doc);
    if (!packed_e) {
        std::fprintf(stderr, "logos_parse_as: compactify failed\n");
        return nullptr;
    }
    auto packed = std::move(packed_e).get();
    auto& packed_arena = logos::writ::WritAccess::arena(packed);
    const uint8_t* data = packed_arena.head().data();
    size_t used = packed_arena.total_used();
    uint8_t* buf = static_cast<uint8_t*>(std::malloc(8 + used));
    if (!buf) return nullptr;
    uint64_t sz = used;
    std::memcpy(buf, &sz, 8);
    std::memcpy(buf + 8, data, used);
    return buf + 8;
}

// Slice 3 test fixture: parses an inline source string and exposes the
// resulting Writ arena bytes through out-params. The handler then calls
// logos_emit_item_blob with those bytes. Goes away once Slice 4's
// quote_item! lands and produces the same shape from in-Logos code.
namespace {
std::vector<logos::writ::Writ> g_test_blob_keepalive;
}
extern "C" int32_t logos_metaprog_test_module_blob(
        const char* src, uint64_t src_len,
        const uint8_t** out_data, uint64_t* out_size) {
    if (!src || !out_data || !out_size) return 0;
    *out_data = nullptr;
    *out_size = 0;
    std::string s(src, src_len);
    logos::compiler::LogosParser parser(s);
    auto ast = parser.parse_module();
    if (ast.is_null() || !parser.at_eof()) {
        std::fprintf(stderr,
            "logos_metaprog_test_module_blob: parse failed near line %u\n",
            parser.next_line());
        return 0;
    }
    auto* h = ast.holder();
    *out_data = h->base();
    *out_size = static_cast<uint64_t>(h->arena().head().used);
    g_test_blob_keepalive.push_back(std::move(ast));
    return 1;
}

// Slice 7 of metaprog-quote: hand-built BIN_OP{LIT_INT(1), "+", LIT_INT(2)}
// blob fixture used to derisk position-aware WRIT_BLOB lowering before the
// real `quote_expr! { ... }` grammar lands. ABI matches WritStatic /
// ExprBlob (returns ptr past an 8-byte size prefix). The buffer is leaked
// intentionally — single-shot per metacall, lifetime is the compile run.
extern "C" const uint8_t* logos_test_make_bin_op_blob() {
    using logos::writ::WritAccess;
    using logos::writ::ArenaString;
    using logos::writ::AnyVal;
    using logos::writ::TinyObjectMap;
    using logos::writ::TinyMapView;
    using logos::writ::ArrayView;
    using logos::writ::arena_offset_t;
    using logos::writ::clone;
    namespace la = logos::compiler::ast;

    // GrowableSingleChunk (single segment for base(doc)+off addressing), pre-sized
    // past any realloc — lazy-zero keeps the reserve cheap.
    auto doc_e = logos::writ::make_doc(size_t(8) * 1024 * 1024,
                                          logos::writ::ArenaMode::GrowableSingleChunk);
    if (!doc_e) return nullptr;
    auto doc = std::move(doc_e).get();

    auto build_lit_int = [&](int64_t v) -> uint32_t {
        auto tm_e = doc.make_tiny_map_view( 4);
        if (!tm_e) return 0;
        uint32_t tm_off = uint32_t(tm_e->offset().value());
        auto str_e = doc.make_string( std::to_string(v));
        if (!str_e) return 0;
        uint32_t str_off = uint32_t(str_e->offset().value());
        auto tm = [&]() {
            return TinyMapView(arena_offset_t(tm_off), doc.holder());
        };
        (void)tm().put(la::CODE.code,
                        AnyVal::from_value<int32_t>(la::LIT_INT.code));
        (void)tm().put(la::VALUE.code,
                        AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(str_off)));
        (void)tm().put(la::SRC_LINE.code,
                        AnyVal::from_value<int32_t>(1));
        tm().set_schema_type_code(
            logos::writ::schema::ast(la::LIT_INT.code));
        return tm_off;
    };

    uint32_t lhs_off = build_lit_int(1);
    uint32_t rhs_off = build_lit_int(2);
    if (!lhs_off || !rhs_off) return nullptr;

    auto op_e = doc.make_string( std::string_view("+"));
    if (!op_e) return nullptr;
    uint32_t op_off = uint32_t(op_e->offset().value());

    auto root_e = doc.make_tiny_map_view( 8);
    if (!root_e) return nullptr;
    uint32_t root_off = uint32_t(root_e->offset().value());
    auto root = [&]() {
        return TinyMapView(arena_offset_t(root_off), doc.holder());
    };
    (void)root().put(la::CODE.code,
                      AnyVal::from_value<int32_t>(la::BINOP.code));
    (void)root().put(la::OP.code,
                      AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(op_off)));
    (void)root().put(la::LHS.code,
                      AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(lhs_off)));
    (void)root().put(la::RHS.code,
                      AnyVal::from_offset(WritAccess::base(doc), arena_offset_t(rhs_off)));
    (void)root().put(la::SRC_LINE.code,
                      AnyVal::from_value<int32_t>(1));
    root().set_schema_type_code(
        logos::writ::schema::ast(la::BINOP.code));

    WritAccess::set_root_offset(doc, arena_offset_t(root_off));

    auto packed_e = compactify(doc);
    if (!packed_e) return nullptr;
    auto packed = std::move(packed_e).get();
    auto& parena = WritAccess::arena(packed);
    const uint8_t* data = parena.head().data();
    uint64_t used = parena.total_used();

    auto* buf = static_cast<uint8_t*>(std::malloc(8 + used));
    if (!buf) return nullptr;
    std::memcpy(buf, &used, 8);
    std::memcpy(buf + 8, data, used);
    return buf + 8;
}

// The ONE canonical host-extern surface for every metaprog JIT — the
// iteration-loop meta_jit AND the compile-mode metacall mc_jit. The two used
// to bind hand-maintained SUBSETS; once archived stdlib code that references
// metaprog externs can enter a thunk module (the std.canon deem items pull
// the wql cone, whose plan lowering references logos_rule_ir), a missing
// binding fails WHOLE-UNIT materialization even when the thunk never calls
// the extern. Binding the full set in both JITs removes the drift class.
static bool bind_metaprog_host_externs(logos::jit::Jit& jit, const char* who) {
    auto bind = [&](const char* name, void* fn) -> bool {
        if (jit.define_symbol(name, fn)) return true;
        std::fprintf(stderr, "logosc: bind %s (%s): %s\n", name, who,
                     jit.error_str().c_str());
        return false;
    };
    return bind("logos_emit_source",               reinterpret_cast<void*>(&logos_emit_source))
        && bind("logos_emit_item_blob",            reinterpret_cast<void*>(&logos_emit_item_blob))
        && bind("logos_emit_item_blob_subst",      reinterpret_cast<void*>(&logos_emit_item_blob_subst))
        && bind("logos_parse_as",                  reinterpret_cast<void*>(&logos_parse_as))
        && bind("logos_qib_pack_idents",           reinterpret_cast<void*>(&logos_qib_pack_idents))
        && bind("logos_qib_free_idents",           reinterpret_cast<void*>(&logos_qib_free_idents))
        && bind("logos_qib_pack_blobs",            reinterpret_cast<void*>(&logos_qib_pack_blobs))
        && bind("logos_qib_free_blobs",            reinterpret_cast<void*>(&logos_qib_free_blobs))
        && bind("logos_qib_pack_cursors",          reinterpret_cast<void*>(&logos_qib_pack_cursors))
        && bind("logos_qib_free_cursors",          reinterpret_cast<void*>(&logos_qib_free_cursors))
        && bind("logos_metaprog_gensym",           reinterpret_cast<void*>(&logos_metaprog_gensym))
        && bind("logos_metacall_freeze2",          reinterpret_cast<void*>(&logos_metacall_freeze2))
        && bind("logos_metaprog_test_module_blob", reinterpret_cast<void*>(&logos_metaprog_test_module_blob))
        && bind("logos_test_make_bin_op_blob",     reinterpret_cast<void*>(&logos_test_make_bin_op_blob))
        && bind("logos_quote_expr_subst",          reinterpret_cast<void*>(&logos_quote_expr_subst))
        && bind("logos_get_module_ast",            reinterpret_cast<void*>(&logos_get_module_ast))
        && bind("logos_get_module_ast_oview",      reinterpret_cast<void*>(&logos_get_module_ast_oview))
        && bind("logos_holder_release",            reinterpret_cast<void*>(&logos_holder_release))
        && bind("logos_metaprog_error",            reinterpret_cast<void*>(&logos_metaprog_error))
        && bind("logos_metaprog_error_at",         reinterpret_cast<void*>(&logos_metaprog_error_at))
        && bind("logos_metaprog_error_located",    reinterpret_cast<void*>(&logos_metaprog_error_located))
        && bind("logos_macro_arg",                 reinterpret_cast<void*>(&logos_macro_arg))
        && bind("logos_rule_ir",                   reinterpret_cast<void*>(&logos_rule_ir));
}

// Round-trip a stack-built LLVM module through textual IR into a fresh
// heap-owned LLVMContext, then hand it to a new logos::jit::Jit. Returns
// the fully initialized Jit or nullptr on failure (errors printed).
// The round-trip is needed because LLJIT::addIRModule wants a
// unique_ptr<LLVMContext>; sites that already own a stack context
// can't move it. Acceptable cost for current module sizes; revisit
// when feeding metaprog-sized programs.
static std::unique_ptr<logos::jit::Jit>
build_jit_from_module(const llvm::Module& src_module, const char* who,
                      bool with_process_symbols = false,
                      const std::vector<std::string>* archives = nullptr) {
    auto jit = std::make_unique<logos::jit::Jit>();
    if (!jit->init()) {
        std::fprintf(stderr, "%s: jit init: %s\n", who, jit->error_str().c_str());
        return nullptr;
    }
    if (with_process_symbols && !jit->enable_process_symbols()) {
        std::fprintf(stderr, "%s: enable_process_symbols: %s\n", who,
                     jit->error_str().c_str());
        return nullptr;
    }
    // M.1 Stage 2 (Mode B): register linked .a archives so metacall callees
    // whose body lives only in compiled form can be resolved by ORC.
    // Registration is unconditional (cheap when unused); no Mode B test
    // exercises this path yet — see ADR M.1 for the deferred coverage.
    if (archives) {
        for (const auto& p : *archives) {
            if (!jit->add_static_archive(p)) {
                std::fprintf(stderr, "%s: add_static_archive(%s): %s\n",
                             who, p.c_str(), jit->error_str().c_str());
                // Non-fatal — Mode A path may still resolve everything.
            }
        }
    }
    auto ctx = std::make_unique<llvm::LLVMContext>();
    std::string ir;
    {
        llvm::raw_string_ostream os(ir);
        src_module.print(os, nullptr);
    }
    llvm::SMDiagnostic diag;
    auto buf = llvm::MemoryBuffer::getMemBuffer(ir);
    auto reparsed = llvm::parseIR(*buf, diag, *ctx);
    if (!reparsed) {
        std::fprintf(stderr, "%s: jit reparse: %s\n", who,
                     diag.getMessage().str().c_str());
        return nullptr;
    }
    if (!jit->add_module(std::move(reparsed), std::move(ctx))) {
        std::fprintf(stderr, "%s: jit add_module: %s\n", who,
                     jit->error_str().c_str());
        return nullptr;
    }
    return jit;
}

// Structured exit codes. lforge depends on these to classify failure
// kinds without parsing diagnostic text.
//
//   0  EXIT_OK             — success
//   1  EXIT_USER_ERROR     — sema / lir / borrow-check / mono error in user code
//   2  EXIT_USAGE          — bad CLI args, manifest-parse failure, missing file
//   3  EXIT_CODEGEN        — mlir-gen / lowering / LLVM IR translation failure
//                            (treated as compiler-internal but triggered by user code)
//   4  EXIT_LINK_IO        — output write / module load / archive read failure
//   5  EXIT_ICE            — internal consistency check / unexpected state
//
// Tests previously relied on `return 1` for any non-success; that still
// holds for sema failures (the 99% case) so `EXIT_USER_ERROR == 1` keeps
// the existing test surface intact.
constexpr int EXIT_OK         = 0;
[[maybe_unused]] constexpr int EXIT_USER_ERROR = 1;
constexpr int EXIT_USAGE      = 2;
[[maybe_unused]] constexpr int EXIT_CODEGEN    = 3;
constexpr int EXIT_LINK_IO    = 4;
[[maybe_unused]] constexpr int EXIT_ICE        = 5;

namespace logos::compiler {

// Shared metaprog discovery loop. Runs sema_lower in metaprog_mode,
// JIT-fires #[derive_*]/handler triggers, processes item-position
// metacalls, splices synthesised items into `asts`, iterates until
// no new emissions. Used by main() (user-code path) and by
// emit_module's compile_to_object (stdlib build path — closes
// debt #21).
//
// Sets g_asts/g_filenames/g_from_binary/g_user_root_idx/g_emit_seen/
// g_any_emitted/g_metaprog_diags/g_ast_provenance from the args;
// restores prior values on exit.
int run_metaprog_dispatch(
    std::vector<writ::Writ>& asts,
    std::vector<std::string>&    filenames,
    std::vector<bool>&           from_binary,
    std::size_t                  entry_ast_idx,
    const MetaprogDispatchOpts&  opts)
{
    constexpr int kMaxMetaprogIters = 16;

    // Save and reset host-side globals on exit so nested or repeated
    // calls don't leak state.
    auto* prev_emit_seen      = g_emit_seen;
    auto* prev_asts           = g_asts;
    auto* prev_filenames      = g_filenames;
    auto* prev_from_binary    = g_from_binary;
    auto  prev_user_root_idx  = g_user_root_idx;
    auto* prev_metaprog_diags = g_metaprog_diags;
    auto* prev_ast_provenance = g_ast_provenance;
    auto* prev_any_emitted    = g_any_emitted;
    struct ScopedRestore {
        std::set<std::string>**             es;
        std::vector<writ::Writ>**       a;
        std::vector<std::string>**          fn;
        std::vector<bool>**                 fb;
        size_t*                             uri;
        std::vector<std::string>**          md;
        std::vector<std::optional<EmitProvenance>>** ap;
        bool**                              ae;
        std::set<std::string>*              p_es;
        std::vector<writ::Writ>*        p_a;
        std::vector<std::string>*           p_fn;
        std::vector<bool>*                  p_fb;
        size_t                              p_uri;
        std::vector<std::string>*           p_md;
        std::vector<std::optional<EmitProvenance>>* p_ap;
        bool*                               p_ae;
        ~ScopedRestore() {
            *es = p_es; *a = p_a; *fn = p_fn; *fb = p_fb;
            *uri = p_uri; *md = p_md; *ap = p_ap; *ae = p_ae;
        }
    } _restore{
        &g_emit_seen, &g_asts, &g_filenames, &g_from_binary,
        &g_user_root_idx, &g_metaprog_diags, &g_ast_provenance,
        &g_any_emitted,
        prev_emit_seen, prev_asts, prev_filenames, prev_from_binary,
        prev_user_root_idx, prev_metaprog_diags, prev_ast_provenance,
        prev_any_emitted,
    };

    std::set<std::string> emit_seen;
    g_emit_seen     = &emit_seen;
    g_asts          = &asts;
    g_filenames     = &filenames;
    g_from_binary   = &from_binary;
    g_user_root_idx = entry_ast_idx;
    // Module system: point the append-time globals at the caller's module_ids
    // vector + own id, restored on exit so nested dispatches don't leak state.
    struct ModIdRestore {
        std::vector<std::string>* prev_mids;
        std::string               prev_self;
        ~ModIdRestore() { g_module_ids = prev_mids; g_self_module_id = std::move(prev_self); }
    } _mid_restore{ g_module_ids, g_self_module_id };
    g_module_ids     = opts.module_ids;
    g_self_module_id = opts.self_module_id;
    // logos_emit_source()/_blob() push NEW asts onto this vector from INSIDE a
    // metaprog handler running mid-sema_lower — which holds a `Writ&` into `asts`.
    // A vector realloc there moves that element (Writ move nulls the source), so the
    // in-flight reference would see a moved-from doc (holder_=header_=0). Reserve up
    // front (no reference held yet) so emit-driven growth never reallocs. (legacy's
    // copyable handle masked this; writ's move-on-realloc exposes it.)
    asts.reserve(asts.size() + 65536);
    filenames.reserve(filenames.size() + 65536);
    // Provenance vector: caller-provided when --dump-metaprog is on
    // (so they can read it post-dispatch); otherwise local & discarded.
    std::vector<std::optional<EmitProvenance>> local_provenance;
    auto* prov = opts.provenance_out ? opts.provenance_out : &local_provenance;
    // Wire when EITHER the dump driver wants it OR a caller passed a sink
    // (emit_module uses it to attribute synth chunks to their source file in
    // per-file mode — see the --only-file filter in emit_module.cpp).
    if (!opts.dump_dir.empty() || opts.provenance_out) g_ast_provenance = prov;

    auto report = [&](const char* label) {
        if (!opts.trace && !std::getenv("LOGOS_TRACE_DISPATCH")) return;
        std::fprintf(stderr, "[metaprog dispatch] %s\n", label);
    };
    auto* stats = opts.stats_out;
    auto stat_step = [&](std::chrono::steady_clock::time_point& t,
                         const char* label, int iter_idx) {
        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - t).count();
        if (stats) stats->add(label, ms, iter_idx);
        t = now;
    };

    SemaOptions meta_opts;
    meta_opts.metaprog_mode = true;
    meta_opts.entry_ast_idx = entry_ast_idx;
    // Phase 2-4: propagate cfg flags into the sema_lower calls inside
    // this dispatcher. cfg_flags come from MetaprogDispatchOpts (the
    // caller — main() — populates them from argv).
    meta_opts.cfg_flags = opts.cfg_flags;
    // M5: same cache pointer is reused by every sema_lower we drive.
    meta_opts.cache     = opts.sema_cache;
    // Skeleton-skip gate: from_binary fns already in a linked archive .o.
    meta_opts.binary_symbols = opts.binary_symbols;
    meta_opts.dep_nominal_decls = opts.dep_nominal_decls;  // G156-1 ambiguity universe
    // Default-on implicit prelude: discovery-pass sema must resolve unqualified
    // prelude names too (the final pass below does the real type resolution,
    // but metaprog handlers may reference Option/Result/String/etc.).
    meta_opts.implicit_prelude = opts.implicit_prelude;
    meta_opts.module_name_to_id = opts.module_name_to_id;  // §B-coex: `use … from` in discovery

    // M6.1: enable incremental dispatch. Cache preserves user-AST state
    // across iters; each iter's sema_lower is given a delta_start_idx so
    // it only processes the NEW asts appended since the previous call.
    // At end of dispatch, reset_user_state() invalidates the user portion
    // of the cache so the *next* sema_lower (final user sema) sees a
    // clean state.
    if (opts.sema_cache) opts.sema_cache->set_keep_user_state(true);
    struct ResetUserStateGuard {
        logos::compiler::SemaCache* c;
        ~ResetUserStateGuard() { if (c) c->reset_user_state(); }
    } _reset_guard{opts.sema_cache};
    // delta_start_idx for the NEXT sema_lower call. Updated to asts.size()
    // right before each sema_lower invocation so the call after it skips
    // everything up through the current asts vector.
    size_t next_delta_start = 0;

    // M6.2: previous iter's mono output, passed to next iter's mono_pass
    // so already-cloned instances + passed-through non-generics are
    // preserved (done_/struct_done_/enum_done_ seeded from this).
    lir::LProgram m6_prev_mono_out;

    // M6.3: persistent metaprog JIT across iters. Iter 0 creates +
    // binds externs + adds its module. Iter N+1 only adds the delta
    // module to the same JIT (which already has iter 0's bodies +
    // bound externs, so unresolved references in the delta module
    // resolve via ORC's existing JITDylib).
    std::unique_ptr<logos::jit::Jit> m6_meta_jit;
    // M6.3: function names emitted by mlir_gen in prior iters of this
    // dispatch loop. Extended into meta_prog.binary_symbols for the
    // current iter's mlir_gen so it forward-declares these names
    // (without re-emitting the body), then accumulated as new
    // emissions are observed.
    StrSet m6_prev_emitted_fns;

    lir::LProgram prog;
    // ADR 0020 wave-0: the previous iteration DEFERRED a language-item site
    // (deem over a pending container's backing). Delta-sema would skip the
    // deferring module entirely — re-run a FULL fresh sema this iteration so
    // the deferred item is revisited (the M6.1 cache-bypass precedent).
    bool retry_deferred = false;
    // Factory configs drained by the EARLY drain below (one chunk per CFG
    // hash); main()'s post-mono drain re-fires them structurally and finds
    // the chunk already emitted, so the two loops never double-generate.
    std::unordered_set<uint64_t>& early_drained_hashes = g_early_drained_hashes;
    bool any_deferral_seen = false;
    for (int iter = 0; ; ++iter) {
        auto _t = std::chrono::steady_clock::now();
        auto opts_iter = meta_opts;
        // M6.1: delta only when we have a cache to provide skipped asts'
        // state. emit_module's dispatch runs without a cache — skipping
        // asts there would lose their symbol-table + LIR contributions
        // (no install_snapshot + no bundle splice to compensate).
        if (opts.sema_cache) {
            if (retry_deferred) {
                opts_iter.cache = nullptr;
                opts_iter.delta_start_idx = 0;
                next_delta_start = asts.size();
                // Fresh TypePool — the previous iter's mono output holds
                // handles into the cached arena; threading it would dangle.
                m6_prev_mono_out = lir::LProgram{};
            } else {
                opts_iter.delta_start_idx = next_delta_start;
                next_delta_start = asts.size();
            }
        }
        retry_deferred = false;
        // Phase 6: metaprog dispatch loop doesn't currently thread is_lazy
        // through (no use case yet — metaprog handlers + their callees stay
        // eager regardless). Pass {} to keep the existing behaviour.
        prog = sema_lower(asts, filenames, from_binary, opts_iter, {},
                          opts.module_ids ? *opts.module_ids : std::vector<std::string>{});
        stat_step(_t, "sema_lower", iter);
        retry_deferred = retry_deferred || prog.has_pending() || prog.deferred_plan_work;
        report(iter == 0 ? "sema+lower" : "sema+lower (re-run)");

        // ── DRAIN FIRST, JUDGE WHAT IS LEFT ─────────────────────────────
        // The one rule, expressed by the ORDER rather than by a flag at each
        // gate: a round a producer can still advance is not judged. Everything
        // downstream of a thing that has not been produced yet is a cascade,
        // and printing it buries the real story under its consequences — which
        // is exactly what happened when the plan loop was judged mid-flight.
        // The drain below is hash-deduped, so it can only fire finitely often;
        // when it stops producing, the gate underneath speaks.
        //
        // The other gates in this file sit OUTSIDE the fixpoint (runner
        // synthesis, borrow check, the terminal re-sema after a drain round has
        // already had its turn), where no producer owes anything, so they judge
        // immediately and should.

        // ── ADR 0021: EARLY factory drain ───────────────────────────────
        // sema records a factory demand the moment anything NAMES
        // `CtrClass<CFG>` (defer_factory_backed). Historically only MONO's
        // demands were drained, in main()'s post-mono loop — so the generated
        // family landed after the last round that could have consumed it, and
        // an ITEM waiting on the family (a `deem` whose source is a family
        // handle; a signature naming one) could never resolve however many
        // times it re-lowered.
        //
        // When a site actually deferred this round, drain the pending demands
        // HERE, inside the fixpoint that discovered them: the chunk is the
        // same one main()'s drain renders, the metacall runs on the next
        // iteration through the ordinary item seam, and the waiting item
        // re-lowers with the family present. Gated on a recorded PENDING
        // so a program with nothing waiting keeps the established ordering
        // exactly; hash-deduped so main()'s later drain is a no-op for
        // anything satisfied here.
        // Sticky: once a site has deferred in THIS dispatch, keep draining
        // demands as they appear — the waiting item may need a family whose
        // demand only surfaces a round later.
        if (prog.has_pending() || prog.deferred_plan_work) any_deferral_seen = true;
        if (any_deferral_seen && !prog.factory_demands.empty()) {
            bool factory_available = false;
            for (const auto& f : prog.functions)
                if (f && logos::compiler::bare_fn_name(f.name())
                         == "__container_factory") { factory_available = true; break; }
            bool drained_now = false;
            bool drain_emitted = false;
            g_any_emitted = &drain_emitted;
            if (factory_available) {
                for (const auto& fd : prog.factory_demands) {
                    if (early_drained_hashes.count(fd.cfg_hash)) continue;
                    auto src_it = prog.wstatic_sources.find(fd.cfg_hash);
                    // No captured CFG source → leave it to main()'s drain,
                    // which owns that diagnostic.
                    if (src_it == prog.wstatic_sources.end()) continue;
                    char hex[17];
                    std::snprintf(hex, sizeof hex, "%016llx",
                                  (unsigned long long)fd.cfg_hash);
                    std::string esc;
                    esc.reserve(src_it->second.size() + 16);
                    for (char c : src_it->second) {
                        switch (c) {
                        case '\\': esc += "\\\\"; break;
                        case '"':  esc += "\\\""; break;
                        case '\n': esc += "\\n";  break;
                        case '\r': esc += "\\r";  break;
                        case '\t': esc += "\\t";  break;
                        case '\0': esc += "\\0";  break;
                        default:   esc += c;      break;
                        }
                    }
                    std::string chunk;
                    chunk += "package logos.gen;\n";
                    chunk += "use logos.lcm.canon.container_item;\n";
                    chunk += "use logos.mem.bt.map;\n";
                    chunk += "metacall __container_factory(\"";
                    chunk += hex;
                    chunk += "\", \"";
                    chunk += esc;
                    chunk += "\");\n";
                    if (logos_emit_source(chunk.c_str())) drained_now = true;
                    early_drained_hashes.insert(fd.cfg_hash);
                    if (opts.trace || std::getenv("LOGOS_TRACE_DISPATCH"))
                        std::fprintf(stderr,
                            "metaclass: factory drained EARLY @hs_%s\n", hex);
                }
            }
            g_any_emitted = nullptr;
            // The chunk is a fresh ast: its metacall site is only visible to
            // the NEXT sema, so force another round rather than falling into
            // the break below.
            if (drained_now) continue;
        }

        prog.print_diags(stderr);
        if (!prog.ok()) return 1;

        bool has_pending_item_mc = false;
        {
            using RT = lir::MetacallRetTag;
            for (const auto& s : prog.metacall_sites) {
                if (s.ret_tag() == RT::ItemBlob) { has_pending_item_mc = true; break; }
            }
        }
        if (prog.metaprog_targets.empty() && !has_pending_item_mc) break;

        if (opts.trace || std::getenv("LOGOS_TRACE_DISPATCH")) {
            std::fprintf(stderr, "[metaprog iter %d] %zu target(s):\n",
                         iter, prog.metaprog_targets.size());
            for (auto& t : prog.metaprog_targets)
                std::fprintf(stderr, "                 - %s\n",
                             std::string(t.trigger()).c_str());
        }

        auto _t2 = std::chrono::steady_clock::now();
        // Reuse the `prog` we already lowered at the top of this iter:
        // the second sema_lower below would do identical work (same asts,
        // same opts, no global state changes between them). The dispatcher
        // loop at line ~2360 still needs metaprog_targets / handlers /
        // functions, but mono_pass consumes meta_prog and drops those
        // fields. So snapshot them now, then move-construct meta_prog
        // from prog.
        // The target/handler/site mirrors are VIEWS into the current
        // program's LIR arena. The item-macro path below REPLACES
        // meta_prog via a fresh sema_lower; without a sema cache keeping
        // the old arena alive (emit_module's stdlib build runs cache-
        // less) that reassignment frees the arena and every saved view
        // dangles — the dispatch then reads empty/garbage names. Deep-
        // copy the fields the dispatch needs into OWNED structs before
        // anything can invalidate them.
        struct OwnedHandler  { std::string trigger, hook_fn; };
        struct OwnedTarget   { size_t ast_idx; uint32_t item_offset;
                               std::string trigger; };
        struct OwnedItemSite { std::string thunk_source, callee_name, thunk_name;
                               size_t ast_idx; uint64_t expr_offset; };
        std::vector<OwnedTarget>  saved_targets;
        std::vector<OwnedHandler> saved_handlers;
        for (const auto& t : prog.metaprog_targets)
            saved_targets.push_back({static_cast<size_t>(t.ast_idx()),
                                     static_cast<uint32_t>(t.item_offset()),
                                     std::string(t.trigger())});
        for (const auto& h : prog.metaprog_handlers)
            saved_handlers.push_back({std::string(h.trigger()),
                                      std::string(h.hook_fn())});
        prog.metaprog_targets.clear();
        prog.metaprog_handlers.clear();
        auto meta_prog      = std::move(prog);
        stat_step(_t2, "meta_sema_lower", iter);
        if (!meta_prog.ok()) { meta_prog.print_diags(stderr); return 1; }

        std::vector<OwnedItemSite> meta_item_sites;
        {
            using RT = lir::MetacallRetTag;
            for (const auto& s : meta_prog.metacall_sites) {
                if (s.ret_tag() == RT::ItemBlob)
                    meta_item_sites.push_back({std::string(s.thunk_source()),
                                               std::string(s.callee_name()),
                                               std::string(s.thunk_name()),
                                               static_cast<size_t>(s.ast_idx()),
                                               static_cast<uint64_t>(s.expr_offset())});
            }
        }
        if (!meta_item_sites.empty()) {
            bool tmp_emitted = false;
            bool* prev_any = g_any_emitted;
            g_any_emitted = &tmp_emitted;
            for (const auto& s : meta_item_sites) {
                if (!s.thunk_source.empty())
                    logos_emit_source_thunk(s.thunk_source.c_str());
            }
            g_any_emitted = prev_any;
            auto resema_opts = meta_opts;
            for (const auto& s : meta_item_sites) {
                if (!s.callee_name.empty())
                    resema_opts.metaprog_keep_fns.push_back(s.callee_name);
            }
            // M6.1: bypass the cache for this re-sema. The cache snapshot
            // captured the iter-top sema where the callee bodies were
            // stubbed (not in metaprog_keep_fns); this re-sema lowers them
            // REAL. Installing the cached state would carry the stubs over
            // and cause duplicate-symbol clashes. cache=nullptr makes this
            // a full fresh sema (slow but correct). next_delta_start stays
            // at its iter-top value so the *next* iter's sema picks up
            // where iter-top left off.
            if (opts.sema_cache) {
                resema_opts.cache = nullptr;
                resema_opts.delta_start_idx = 0;
            }
            meta_prog = sema_lower(asts, filenames, from_binary, resema_opts, {},
                                   opts.module_ids ? *opts.module_ids : std::vector<std::string>{});
            if (!meta_prog.ok()) { meta_prog.print_diags(stderr); return 1; }
            retry_deferred = retry_deferred || meta_prog.has_pending();
        }
        // WQL/Trama raw-text item macros: a #[token_macro] `(str) -> ItemList`
        // item callee's thunk reads its block bytes via logos_macro_arg(site,0).
        // mono_pass (below) discards macro_arg_blobs, so snapshot the handle now
        // — it stays valid because the underlying arena lives in `asts`. We
        // install g_macro_args from this snapshot around the item-thunk loop.
        auto m6_saved_macro_args = meta_prog.macro_arg_blobs;
        // Metaprog stubs (bodies the discovery pass skipped). A CONCRETE stub
        // is kept and POISONED — mlir_gen gives it a trap body — instead of
        // being dropped: dropping it dangles any caller that WAS lowered in
        // this pass, and callers exist. A `deem` item's query fn is emitted as
        // SOURCE (so it is lowered, and Canon's own compile-time queries need
        // it to be) while the container family it reads arrives as item BLOBS
        // (so it is stubbed) — the call between them has to land on a symbol.
        // A trap body is the honest one: nothing at compile time calls a
        // generated container's materializer, and if anything ever did, it
        // would abort rather than silently do nothing.
        // GENERIC stubs still go: mono instantiates from the body, and an
        // empty one has nothing to clone.
        for (auto& f : meta_prog.functions)
            if (f && f.is_metaprog_stub() && f.type_params().empty())
                meta_prog.poisoned_fns.insert(
                    sym::link_name(f, meta_prog.pkg_module_ids));
        meta_prog.functions.erase(
            std::remove_if(meta_prog.functions.begin(), meta_prog.functions.end(),
                [](const auto& f) {
                    return f.is_metaprog_stub() && !f.type_params().empty();
                }),
            meta_prog.functions.end());
        // Pass binary_symbols through so the metaprog JIT's mlir_gen skips
        // body emission for fns already provided by the JIT's archive
        // generators (resolved via build_jit_from_module's archive_paths).
        for (auto& __s : opts.binary_symbols) logos::compiler::lir_mirror_map_put_null(meta_prog, meta_prog.binary_symbols, __s);
        // M6.3: extend binary_symbols with the names already emitted by
        // prior iters' mlir_gen. mlir_gen will forward-declare these but
        // skip body emission — the bodies live in m6_meta_jit (from those
        // prior iters' addModule calls) and resolve through ORC's
        // existing JITDylib at link time.
        for (auto& n : m6_prev_emitted_fns) logos::compiler::lir_mirror_map_put_null(meta_prog, meta_prog.binary_symbols, n);
        auto _t3 = std::chrono::steady_clock::now();
        meta_prog = reflection_emit(std::move(meta_prog));
        stat_step(_t3, "reflection", iter);
        {
            // M6.2: thread prev iter's mono output for incremental clone.
            // ONLY when the sema cache provides shared TypePool + pools
            // across iters — without that (emit_module's stdlib build runs
            // without a cache), each iter's prog has a fresh TypePool and
            // prev_out's mirror_ptr_ values reference iter N-1's now-
            // dead arena.
            MonoOpts mopts_iter;
            if (opts.sema_cache) {
                mopts_iter.prev_out = std::move(m6_prev_mono_out);
            }
            meta_prog = mono_pass(std::move(meta_prog), std::move(mopts_iter));
        }
        stat_step(_t3, "mono", iter);
        if (!meta_prog.ok()) { meta_prog.print_diags(stderr); return 1; }
        meta_prog = borrow_check(std::move(meta_prog));
        stat_step(_t3, "borrow", iter);
        if (!meta_prog.ok()) { meta_prog.print_diags(stderr); return 1; }

        mlir::MLIRContext meta_mlir_ctx;
        meta_mlir_ctx.getOrLoadDialect<mlir::func::FuncDialect>();
        meta_mlir_ctx.getOrLoadDialect<mlir::arith::ArithDialect>();
        meta_mlir_ctx.getOrLoadDialect<mlir::scf::SCFDialect>();
        meta_mlir_ctx.getOrLoadDialect<mlir::cf::ControlFlowDialect>();
        meta_mlir_ctx.getOrLoadDialect<mlir::LLVM::LLVMDialect>();
        auto meta_mlir = mlir_gen(meta_mlir_ctx, meta_prog);
        if (!meta_mlir) { std::fprintf(stderr, "logosc: metaprog MLIR gen failed\n"); return 1; }
        stat_step(_t3, "mlir_gen", iter);
        // M6.3: record functions emitted by THIS iter's mlir_gen so the
        // next iter's binary_symbols extension covers them. "Emitted"
        // means name is in prog.functions / struct.methods AND NOT in
        // the current binary_symbols set (which already excludes them).
        // Module system: track the QUALIFIED LINK names (the same form mlir_gen
        // emits + is_binary_skip checks) via the single shared sym::link_name —
        // else methods (qualified at emission) desync from the bare tracking and
        // get re-emitted across iters → ORC duplicate-definition.
        for (auto& fp : meta_prog.functions) {
            if (!fp) continue;
            auto ln = sym::link_name(fp, meta_prog.pkg_module_ids);
            if (!meta_prog.binary_symbols.has(ln))
                m6_prev_emitted_fns.insert(std::move(ln));
        }
        for (auto& sd : meta_prog.structs) {
            sd.each_method([&](lir_view::FunctionView m) {
                if (!m) return;
                auto ln = sym::link_name(m, meta_prog.pkg_module_ids);
                if (!meta_prog.binary_symbols.has(ln))
                    m6_prev_emitted_fns.insert(std::move(ln));
            });
        }
        mlir::PassManager meta_pm(&meta_mlir_ctx);
        meta_pm.addPass(logos::compat::create_scf_to_cf_pass());
        meta_pm.addPass(mlir::createConvertControlFlowToLLVMPass());
        meta_pm.addPass(mlir::createArithToLLVMConversionPass());
        meta_pm.addPass(mlir::createConvertFuncToLLVMPass());
        meta_pm.addPass(mlir::createReconcileUnrealizedCastsPass());
        if (mlir::failed(meta_pm.run(*meta_mlir))) {
            std::fprintf(stderr, "logosc: metaprog MLIR lowering failed\n"); return 1;
        }
        stat_step(_t3, "mlir->llvm", iter);
        mlir::registerBuiltinDialectTranslation(meta_mlir_ctx);
        mlir::registerLLVMDialectTranslation(meta_mlir_ctx);
        // M6.3: heap-allocated LLVMContext so we can move ownership into
        // m6_meta_jit's ORC ThreadSafeModule without round-tripping the IR
        // through textual form (as build_jit_from_module does for the
        // const& Module case).
        auto meta_llvm_ctx_ptr = std::make_unique<llvm::LLVMContext>();
        auto meta_llvm = mlir::translateModuleToLLVMIR(*meta_mlir, *meta_llvm_ctx_ptr);
        if (!meta_llvm) { std::fprintf(stderr, "logosc: metaprog LLVM IR translate failed\n"); return 1; }
        stat_step(_t3, "llvm_ir", iter);
        logos::compat::set_default_target_triple(*meta_llvm);
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();

        // M6.3: build JIT on first iter (bind externs + add module), reuse
        // on subsequent iters (just add module). The delta module's forward-
        // declared symbols resolve against prior iters' modules via ORC.
        if (!m6_meta_jit) {
            m6_meta_jit = std::make_unique<logos::jit::Jit>();
            if (!m6_meta_jit->init()) {
                std::fprintf(stderr, "logosc-metaprog: jit init: %s\n",
                             m6_meta_jit->error_str().c_str());
                return 1;
            }
            if (!m6_meta_jit->enable_process_symbols()) {
                std::fprintf(stderr, "logosc-metaprog: enable_process_symbols: %s\n",
                             m6_meta_jit->error_str().c_str());
                return 1;
            }
            for (const auto& p : opts.archive_paths) {
                if (!m6_meta_jit->add_static_archive(p)) {
                    std::fprintf(stderr, "logosc-metaprog: add_static_archive(%s): %s\n",
                                 p.c_str(), m6_meta_jit->error_str().c_str());
                    // Non-fatal (matches build_jit_from_module).
                }
            }
            // The canonical extern set (shared with the compile-mode
            // metacall JIT — see bind_metaprog_host_externs). Includes the
            // per-site macro-arg accessor for #[token_macro] item thunks
            // (g_macro_args is set around the item-thunk loop below) and
            // the rule-IR handoff.
            if (!bind_metaprog_host_externs(*m6_meta_jit, "meta_jit")) return 1;
        }
        if (!m6_meta_jit->add_module(std::move(meta_llvm), std::move(meta_llvm_ctx_ptr))) {
            std::fprintf(stderr, "logosc-metaprog: jit add_module: %s\n",
                         m6_meta_jit->error_str().c_str());
            return 1;
        }
        stat_step(_t3, "jit_build", iter);
        report("metaprog jit");

        bool any_emitted = false;
        g_any_emitted = &any_emitted;
        std::vector<std::string> hook_diags;
        g_metaprog_diags = &hook_diags;
        // M6.3: alias to keep downstream lookups (meta_jit->lookup) reading
        // from the persistent jit.
        auto& meta_jit = m6_meta_jit;

        // Use the saved targets/handlers (snapshotted from prog above)
        // because mono_pass dropped them from meta_prog. meta_prog.functions
        // is preserved through mono (names match prog's pre-mono names for
        // free fns; mono just renames mangled instances).
        for (const auto& tgt : saved_targets) {
            // Route oview_module_ast at this trigger's ast (handler may
            // be in a sibling module — emit_module case dispatches across
            // multiple non-binary asts in one pass). Restored after the
            // inner per-handler loop.
            auto saved_root = g_user_root_idx;
            g_user_root_idx = tgt.ast_idx;
            bool any_handler = false;
            std::string tgt_trigger(tgt.trigger);
            for (const auto& mh : saved_handlers) {
                if (mh.trigger != tgt_trigger) continue;
                any_handler = true;
                std::string mh_hook_fn(mh.hook_fn);
                std::string lookup_name = mh_hook_fn;
                for (const auto& f : meta_prog.functions) {
                    if (bare_fn_name(f.name()) == mh_hook_fn) {
                        lookup_name = std::string(f.name());
                        break;
                    }
                }
                auto* sym = meta_jit->lookup(lookup_name);
                if (!sym) {
                    std::fprintf(stderr, "logosc: metaprog hook lookup '%s': %s\n",
                                 lookup_name.c_str(), meta_jit->error_str().c_str());
                    g_user_root_idx = saved_root;
                    return 1;
                }
                g_current_hook_name = mh_hook_fn.c_str();
                {
                    int line = 0;
                    std::string target_name;
                    if (tgt.ast_idx < asts.size()) {
                        auto* h    = asts[tgt.ast_idx].holder();
                        auto tom   = writ::TinyMapView(
                                        writ::arena_offset_t(tgt.item_offset), h);
                        auto av = tom.get(ast::SRC_LINE.code);
                        if (!av.is_null() && av.is_value())
                            line = static_cast<int>(av.as_value<uint32_t>());
                        auto nm_av = tom.get(ast::NAME.code);
                        if (!nm_av.is_null()) {
                            auto sv = writ::StringView(
                                nm_av, h).view();
                            target_name = std::string(sv);
                        }
                    }
                    g_current_emit_ctx = EmitProvenance{
                        tgt.ast_idx < filenames.size() ? filenames[tgt.ast_idx] : std::string{},
                        line, mh_hook_fn, tgt_trigger, target_name, iter,
                    };
                    g_current_emit_ctx_valid = true;
                }
                reinterpret_cast<void (*)(uint32_t)>(sym)(tgt.item_offset);
                g_current_emit_ctx_valid = false;
                g_current_hook_name = nullptr;
            }
            g_user_root_idx = saved_root;
            if (!any_handler) {
                std::fprintf(stderr,
                    "logosc: internal: no handler for trigger '%s'\n",
                    tgt_trigger.c_str());
                return 1;
            }
        }
        if (!hook_diags.empty()) {
            for (const auto& d : hook_diags)
                std::fprintf(stderr, "error: %s\n", d.c_str());
            return 1;
        }

        // Publish this iter's per-site arg-blob table so raw-text item macro
        // thunks (#[token_macro] `(str) -> ItemList`) can resolve their block
        // bytes via logos_macro_arg(site, 0). Cleared right after the loop.
        g_macro_args = &m6_saved_macro_args;
        struct M6MacroArgsGuard {
            // The rule-IR docs are handed to handlers as raw pointers; they die with
            // the dispatch loop, exactly like the macro-arg blobs.
            ~M6MacroArgsGuard() { g_macro_args = nullptr; logos::compiler::rule_ir::clear(); }
        } m6_macro_args_guard;
        for (const auto& site : meta_item_sites) {
            if (site.thunk_source.empty()) continue;
            if (site.ast_idx >= asts.size()) continue;
            auto* sym = meta_jit->lookup(std::string(site.thunk_name));
            if (!sym) {
                // Sites are re-collected by THIS iteration's sema, so the
                // thunk must be in this iteration's JIT module — a lookup
                // miss is a materialization failure. Silently skipping it
                // leaves the item un-flipped forever: the loop exits on
                // !any_emitted with the item still pending and every fn of
                // its module demoted to a trap stub in the final program.
                std::fprintf(stderr,
                    "logosc: metaprog item-thunk lookup '%s' (%s): %s\n",
                    std::string(site.thunk_name).c_str(),
                    std::string(site.callee_name).c_str(),
                    meta_jit->error_str().c_str());
                return 1;
            }
            // Route oview_module_ast at the TRIGGER module's ast for the
            // handler run (mirrors the derive dispatch above). Single-
            // program builds got this for free (entry idx == the one user
            // module); emit_module passes the -1 sentinel, which reads as
            // a null OView inside the hook and crashes reflection.
            auto saved_root = g_user_root_idx;
            g_user_root_idx = site.ast_idx;
            // Site provenance for the thunk run: handler/walker `error()`
            // calls (the whole deem diagnostic surface) prefix `<file>:<line>`
            // from this — the item's own SRC_LINE, not whatever fn sema last
            // visited.
            {
                int line = 0;
                auto* ph = asts[site.ast_idx].holder();
                auto ptom = logos::writ::TinyMapView(
                    logos::writ::arena_offset_t(site.expr_offset), ph);
                auto av = ptom.get(logos::compiler::ast::SRC_LINE.code);
                if (!av.is_null() && av.is_value())
                    line = static_cast<int>(av.as_value<uint32_t>());
                g_current_emit_ctx = EmitProvenance{
                    site.ast_idx < filenames.size() ? filenames[site.ast_idx]
                                                    : std::string{},
                    line, site.callee_name, std::string{}, std::string{}, iter,
                };
                g_current_emit_ctx_valid = true;
            }
            reinterpret_cast<void (*)()>(sym)();
            g_current_emit_ctx_valid = false;
            g_user_root_idx = saved_root;
            auto& doc = asts[site.ast_idx];
            auto* h    = doc.holder();
            auto tom  = logos::writ::TinyMapView(logos::writ::arena_offset_t(site.expr_offset), h);
            // Same DONE-marker selection as the compile-mode loop: a consumed
            // MAPPING keeps its identity (MAPPING_DEF_DONE) so a consumer
            // compiling against this archive can register its rules for
            // cross-module fusion (ADR 0016).
            int32_t done_code2 = ast::METACALL_ITEM_DONE.code;
            {
                auto cav = tom.get(ast::CODE.code);
                if (!cav.is_null() && cav.is_value()) {
                    int32_t cur = cav.as_value<int32_t>();
                    if (cur == ast::FN_MACRO_CALL_ITEM.code
                        || cur == ast::DEEM_DEF.code)
                        done_code2 = ast::FN_MACRO_CALL_ITEM_DONE.code;
                    if (cur == ast::MAPPING_DEF.code)
                        done_code2 = ast::MAPPING_DEF_DONE.code;
                    if (cur == ast::CONTAINER_DEF.code)
                        done_code2 = ast::CONTAINER_DEF_DONE.code;
                }
            }
            if (auto r = tom.put(
                    ast::CODE.code,
                    writ::AnyVal::from_value<int32_t>(done_code2)
                    ); !r) {
                std::fprintf(stderr,
                    "logosc: metacall item-splice (loop): CODE put failed\n");
                return 1;
            }
        }
        // Raw-text item-macro thunks (#[token_macro] — wql!/trama!) report
        // diagnostics through the same `error()` channel as metaprog hooks;
        // hook_diags was drained (and required empty) after the hook loop
        // above, so anything here came from THESE thunks. Fail the compile.
        if (!hook_diags.empty()) {
            for (const auto& d : hook_diags)
                std::fprintf(stderr, "error: %s\n", d.c_str());
            return 1;
        }

        if (!any_emitted) break;
        if (iter + 1 >= kMaxMetaprogIters) {
            std::fprintf(stderr,
                "logosc: metaprog loop did not converge in %d iterations\n",
                kMaxMetaprogIters);
            return 1;
        }
        // M6.2: save mono output for next iter's incremental mono. Done
        // at end of iter (after hook dispatch read meta_prog.functions
        // for the JIT lookup). Move is safe: meta_prog/JIT are torn down
        // when this iter scope exits anyway.
        m6_prev_mono_out = std::move(meta_prog);
    }
    return 0;
}

} // namespace logos::compiler

// ── ABI spec emitter (`--emit-abi`) ──────────────────────────────────────────
// Emit the compiler's binary-ABI surface as a canonical, one-record-per-line,
// lexically-sorted spec so its git history IS the ABI changelog and a semantic
// differ (abi-analyze) can pair records by key. Record = "<cat>\t<key>[\t<detail>]".
// Categories grow incrementally; v1 covers:
//   schema <name> <ver>   serialization-format versions (compiled-in)   [cat 5]
//   sym    <mangled>      stdlib exported symbol (mangling encodes sig)  [cat 1]
// A removed `sym` line = ABI break (consumer can't link it); an added one is
// compatible. Type layouts (cat 2) + vtables (cat 3) + C++ boundary structs
// (cat 4) are appended in later steps.
static int emit_abi_spec(const std::vector<std::string>& lib_dirs,
                         const std::vector<std::string>& lib_files,
                         const std::string& out_path) {
    std::set<std::string> records;
    // cat 5: serialization-format versions (single source: these constants).
    records.insert("schema\twrit0_format\t3");
    records.insert("schema\tlir_arena_root\t"
        + std::to_string(logos::writ::lir_arena_root::CURRENT_VERSION));

    // cat 4: layout of the Writ types the compiler bakes into binary artifacts
    // (the .writ0 / LIR-blob / arena format). A size/alignment change to any of
    // these breaks every previously-written blob — record sizeof+alignof so the
    // analyzer flags it. logosc links Writ, so it emits these directly (no
    // separate offsetof tool). "everything from Writ the compiler uses" = the
    // value encodings, headers, in-arena container headers, and table entries that
    // define the on-disk format.
    {
#define LOGOS_ABI_TYPE(T) records.insert("type\t" #T "\tsize=" \
        + std::to_string(sizeof(T)) + " align=" + std::to_string(alignof(T)))
        LOGOS_ABI_TYPE(logos::writ::AnyVal);          // the 8-byte tagged value word
        LOGOS_ABI_TYPE(logos::writ::ExternalRef);     // decoded cross-arena (arena_id, obj_id)
        LOGOS_ABI_TYPE(logos::writ::TypeTag);         // per-object in-arena tag
        LOGOS_ABI_TYPE(logos::writ::DocumentHeader);  // blob root header
        LOGOS_ABI_TYPE(logos::writ::Chunk);           // arena chunk header
        LOGOS_ABI_TYPE(logos::writ::ArenaString);     // in-arena string header
        LOGOS_ABI_TYPE(logos::writ::ObjectArray);     // in-arena array header
        LOGOS_ABI_TYPE(logos::writ::ObjectMap);       // in-arena map header
        LOGOS_ABI_TYPE(logos::writ::TinyObjectMap);   // in-arena tiny-map (schema objects)
        LOGOS_ABI_TYPE(logos::writ::MapEntry);        // map slot layout
        LOGOS_ABI_TYPE(logos::writ::ImportEntry);     // .imp table entry
#undef LOGOS_ABI_TYPE
        // Field offsets for the public-field format structs — a reorder at
        // constant size (invisible to size/align) is caught here. The container
        // headers (AnyVal/ObjectMap/…) have private members so offsetof can't
        // reach them; their size/align above is their load-bearing invariant.
        auto off_rec = [&](const char* name, const std::string& fields) {
            records.insert("type\t" + std::string(name) + "::offsets\t[" + fields + "]");
        };
        off_rec("logos::writ::ExternalRef",
            "aid@" + std::to_string(offsetof(logos::writ::ExternalRef, aid)) +
            ",oid@" + std::to_string(offsetof(logos::writ::ExternalRef, oid)));
        off_rec("logos::writ::DocumentHeader",
            "root@" + std::to_string(offsetof(logos::writ::DocumentHeader, root)));
        off_rec("logos::writ::MapEntry",
            "key@" + std::to_string(offsetof(logos::writ::MapEntry, key)) +
            ",val@" + std::to_string(offsetof(logos::writ::MapEntry, val)));
    }

    // Public-symbol allowlist: emit_module wrote a `.abi-pub` sidecar next to
    // each stdlib archive listing the mangled link-symbol of every PUBLIC item
    // (pub fn / pub method + their monomorphised instances), named by the SAME
    // canonical mangler (sym::link_name) that codegen uses — so these match `nm`
    // byte-for-byte. We SCOPE the `sym` records to this set: module-PRIVATE fns
    // get EXTERNAL linkage in Logos, so without this filter the spec tracks
    // internal helpers and a private-helper refactor reads as an ABI break.
    // Layouts/vtables/writ-types/schemas (cat 2/3/4/5) are already curated —
    // untouched. If no sidecar is found (e.g. an old build tree), the set stays
    // empty and we fall back to recording ALL external symbols (fail-open: never
    // silently drop a public symbol because a sidecar is missing).
    std::set<std::string> pub_syms;
    {
        auto read_pub = [&](const std::string& glob) {
            FILE* pipe = ::popen(("cat " + glob + " 2>/dev/null").c_str(), "r");
            if (!pipe) return;
            char line[2048];
            while (std::fgets(line, sizeof(line), pipe)) {
                std::string_view sv(line);
                while (!sv.empty() && (sv.back()=='\n'||sv.back()=='\r')) sv.remove_suffix(1);
                if (!sv.empty()) pub_syms.insert(std::string(sv));
            }
            ::pclose(pipe);
        };
        for (const auto& d : lib_dirs) read_pub(d + "/*.abi-pub");
        for (const auto& f : lib_files) read_pub(f + ".abi-pub");
    }
    const bool have_pub_allowlist = !pub_syms.empty();

    // cat 1: stdlib exported symbols. nm -p (no sort — we sort canonically via
    // the set). The mangled name encodes the signature, so a removal/signature
    // change surfaces as a dropped line. Scoped to the PUBLIC allowlist above.
    auto add_syms = [&](const std::string& cmd) {
        FILE* pipe = ::popen(cmd.c_str(), "r");
        if (!pipe) return;
        char line[1024];
        while (std::fgets(line, sizeof(line), pipe)) {
            std::string_view sv(line);
            while (!sv.empty() && (sv.back()=='\n'||sv.back()=='\r'||sv.back()==' '||sv.back()=='\t'))
                sv.remove_suffix(1);
            // Skip archive/path lines and assembler-local labels (.L.str, .Ltmp):
            // the ABI surface is the EXTERNAL defined symbols a consumer links.
            // Also skip `_binary_*` ld embedding markers (start/end/size for the
            // embedded .writ0): their names encode the /tmp emit PATH, so they
            // are build-location-dependent — not a portable ABI record.
            if (sv.empty() || sv.front() == '/' || sv.front() == '.'
                || sv.rfind("_binary_", 0) == 0)
                continue;
            // Public-scope filter: keep only symbols on the pub allowlist. When
            // no allowlist was found, keep everything (fail-open).
            if (have_pub_allowlist && !pub_syms.count(std::string(sv)))
                continue;
            records.insert("sym\t" + std::string(sv));
        }
        ::pclose(pipe);
    };
    // --extern-only: external (global) defined symbols = the link-time ABI.
    // Scope to liblogos-*.a (the Logos stdlib). The liblstdlib_* support libs
    // (rt/fibers — runtime) belong to cat 4 (C++ boundary, handled separately)
    // and vendored liburing is a dep's ABI, not Logos's — both excluded here.
    for (const auto& d : lib_dirs)
        add_syms("nm --defined-only --extern-only -p -j " + d + "/liblogos-*.a 2>/dev/null");
    for (const auto& f : lib_files)
        add_syms("nm --defined-only --extern-only -p -j " + f + " 2>/dev/null");

    // cat 2 (Logos type layouts) + cat 3 (vtables): merge the *.abi-layout
    // sidecars emit_module wrote next to the stdlib objects (it has the decl
    // views post-sema; --emit-abi does not). Each line is already a canonical
    // record, so just fold it into the set.
    auto add_layout_dir = [&](const std::string& d) {
        FILE* pipe = ::popen(("cat " + d + "/*.abi-layout 2>/dev/null").c_str(), "r");
        if (!pipe) return;
        char line[4096];
        while (std::fgets(line, sizeof(line), pipe)) {
            std::string_view sv(line);
            while (!sv.empty() && (sv.back()=='\n'||sv.back()=='\r')) sv.remove_suffix(1);
            if (!sv.empty()) records.insert(std::string(sv));
        }
        ::pclose(pipe);
    };
    for (const auto& d : lib_dirs) add_layout_dir(d);

    FILE* out = out_path.empty() ? stdout : std::fopen(out_path.c_str(), "w");
    if (!out) {
        std::fprintf(stderr, "logosc: --emit-abi: cannot open '%s'\n", out_path.c_str());
        return 1;
    }
    std::fprintf(out, "# Logos ABI spec — generated by `logosc --emit-abi`; DO NOT EDIT.\n");
    std::fprintf(out, "# git history of this file IS the ABI changelog; analyze with abi-analyze.\n");
    std::fprintf(out, "# record = <category>\\t<key>[\\t<detail>], one per line, lexically sorted.\n");
    for (const auto& r : records) std::fprintf(out, "%s\n", r.c_str());
    if (out != stdout) std::fclose(out);
    return 0;
}

// ── ABI semantic differ (`--abi-diff <old.abi> <new.abi>`) ───────────────────
// Qualify the change between two ABI specs as ABI-preserving (patchset) or
// ABI-breaking (minor bump). NOT a raw line diff: pairs records by (category,
// key) and applies per-category compatibility rules. Exit 0 = preserving,
// 1 = breaking (the minor-bump CI gate), 2 = usage/IO error.
//   git usage: logosc --abi-diff <(git show v0.1:abi/logos.abi) abi/logos.abi
static int abi_diff(const std::string& old_path, const std::string& new_path) {
    // `#[repr(transparent)]` UnsafeCell<X> has the EXACT ABI of X. Normalize it
    // away in BOTH specs before comparing so that wrapping/unwrapping a field in
    // UnsafeCell (interior-mutability marking) is never read as an ABI change —
    // and so a spec emitted by an older logosc (which printed `UnsafeCell<X>`)
    // compares equal to one emitted by a newer logosc (which unwraps it). Handles
    // nesting / multiple occurrences via matched-angle-bracket removal.
    auto strip_transparent = [](std::string s) {
        const std::string pfx = "UnsafeCell<";
        size_t p;
        while ((p = s.find(pfx)) != std::string::npos) {
            size_t open = p + pfx.size() - 1;          // index of '<'
            int depth = 0; size_t i = open;
            for (; i < s.size(); ++i) {
                if (s[i] == '<') ++depth;
                else if (s[i] == '>' && --depth == 0) break;
            }
            if (i >= s.size()) break;                  // malformed — leave as-is
            s = s.substr(0, p) + s.substr(open + 1, i - open - 1) + s.substr(i + 1);
        }
        return s;
    };
    // Parse a spec into (category,key) -> detail. Records are "<cat>\t<key>[\t<detail>]".
    auto load = [&strip_transparent](const std::string& p,
                   std::map<std::pair<std::string,std::string>, std::string>& out) -> bool {
        FILE* f = std::fopen(p.c_str(), "r");
        if (!f) { std::fprintf(stderr, "logosc: --abi-diff: cannot open '%s'\n", p.c_str()); return false; }
        char line[2048];
        while (std::fgets(line, sizeof(line), f)) {
            std::string_view sv(line);
            while (!sv.empty() && (sv.back()=='\n'||sv.back()=='\r')) sv.remove_suffix(1);
            if (sv.empty() || sv.front() == '#') continue;
            auto t1 = sv.find('\t');
            if (t1 == std::string_view::npos) continue;
            std::string cat(sv.substr(0, t1));
            std::string_view rest = sv.substr(t1 + 1);
            auto t2 = rest.find('\t');
            std::string key(t2 == std::string_view::npos ? rest : rest.substr(0, t2));
            std::string det(t2 == std::string_view::npos ? std::string_view{} : rest.substr(t2 + 1));
            out[{cat, key}] = strip_transparent(det);
        }
        std::fclose(f);
        return true;
    };
    std::map<std::pair<std::string,std::string>, std::string> a, b;
    if (!load(old_path, a) || !load(new_path, b)) return 2;

    // Per-category verdict for a record that was removed or whose detail changed.
    // Returns true if the change BREAKS ABI.
    auto removal_breaks = [](const std::string& cat) {
        // sym: a dropped external symbol can't be linked → break.
        // schema: a dropped format version → break.
        // (future cats type/vtable: removal/incompatible-change → break.)
        return cat == "sym" || cat == "schema" || cat == "type" || cat == "vtable";
    };
    auto change_breaks = [](const std::string& cat, const std::string& oldd, const std::string& newd) {
        if (cat == "schema") {
            // Forward-readable by design: a version INCREASE is compatible (newer
            // compiler reads older artifacts); a decrease breaks.
            long ov = std::strtol(oldd.c_str(), nullptr, 10);
            long nv = std::strtol(newd.c_str(), nullptr, 10);
            return nv < ov;
        }
        // sym has no detail (key is the whole symbol). type/vtable layout change → break.
        return true;
    };

    std::vector<std::string> removed, changed_break, changed_compat, added;
    for (const auto& [k, det] : a) {
        auto it = b.find(k);
        if (it == b.end()) {
            std::string rec = k.first + " " + k.second;
            removed.push_back(rec + (removal_breaks(k.first) ? "  [BREAKING]" : ""));
            continue;
        }
        if (it->second != det) {
            std::string rec = k.first + " " + k.second + "  (" + det + " -> " + it->second + ")";
            (change_breaks(k.first, det, it->second) ? changed_break : changed_compat).push_back(rec);
        }
    }
    for (const auto& [k, det] : b)
        if (!a.count(k)) added.push_back(k.first + " " + k.second);

    size_t n_break = 0;
    for (const auto& r : removed) if (r.find("[BREAKING]") != std::string::npos) ++n_break;
    n_break += changed_break.size();

    auto dump = [](const char* title, const std::vector<std::string>& v) {
        if (v.empty()) return;
        std::printf("%s (%zu):\n", title, v.size());
        for (const auto& r : v) std::printf("  %s\n", r.c_str());
    };
    std::printf("ABI diff: %s -> %s\n", old_path.c_str(), new_path.c_str());
    dump("REMOVED", removed);
    dump("CHANGED (breaking)", changed_break);
    dump("CHANGED (compatible)", changed_compat);
    std::printf("ADDED: %zu record(s)\n", added.size());
    if (n_break) {
        std::printf("VERDICT: ABI-BREAKING — minor bump required (%zu breaking change(s))\n", n_break);
        return 1;
    }
    std::printf("VERDICT: ABI-PRESERVING — additive only, patchset OK\n");
    return 0;
}

static void print_usage(std::FILE* out) {
    std::fprintf(out,
"usage: logosc <input.logos> [options]\n"
"\n"
"Compile a Logos source file to a native object (default), or run/inspect it.\n"
"\n"
"Output:\n"
"  -o <file>              output path (default: output.o)\n"
"  -O0 | -O1 | -O2 | -O3  optimization level (default: -O0)\n"
"  -C overflow-checks=off integer +/-/* wrap silently instead of trapping (default: on)\n"
"  -C target-cpu=native   emit code for the host CPU (AVX/AVX2/AVX-512; default: generic)\n"
"  -g, --debug            emit DWARF debug info\n"
"  --emit-mlir            emit MLIR instead of an object\n"
"  --emit-llvm            emit LLVM IR (PRE-optimization) instead of an object\n"
"  --emit-llvm-opt        emit LLVM IR AFTER the opt pipeline (honors -O) instead of an object\n"
"\n"
"Modules & libraries:\n"
"  -L, --libs <dir>       add a binary-module search directory\n"
"  -l, --lib <file>       link a specific binary-module archive (.a)\n"
"  -I <dir>               source-include dir (only with --emit-module)\n"
"  --no-system            don't append the system stdlib search path\n"
"  --emit-module <man>    build a module archive from a .module manifest\n"
"  --only-file <file>     (with --emit-module) compile just one source file\n"
"\n"
"Metaprogramming:\n"
"  --expand               expand metaprograms and stop\n"
"  --gen-dir <dir>        write metaprog-emitted modules as real .gen.logos\n"
"                         files (reparsed back in: sources ARE the debug info)\n"
"  --dump-metaprog[=dir]  dump metaprog-emitted ASTs as Logos source\n"
"  --dump-metaprog-filter[=sel]  filter which metacalls are dumped\n"
"  --cfg <feature>        enable a cfg feature (repeatable)\n"
"\n"
"Diagnostics & info:\n"
"  --diag-format=text|json   diagnostic output format (default: text)\n"
"  --stats                print phase timing stats\n"
"  -V, --version          print version\n"
"  --print-prefix         print this version's tree root\n"
"  --print-lib-dir        print this version's stdlib dir\n"
"  --print-metadata       print version/slot/prefix/lib_dir (Writ doc)\n"
"  -h, --help             show this help\n"
"\n"
"ABI tracking:\n"
"  --emit-abi             dump the stdlib binary-ABI surface\n"
"  --abi-diff <old> <new> qualify an ABI change (preserving vs breaking)\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(stderr);
        return EXIT_USAGE;
    }

    // Initialize Writ TypeOps registry; the @-literal builder uses
    // clone() which dispatches per-type via this registry.
    logos::writ::writ_init();

    const char* input_path = nullptr;
    const char* output_path = "output.o";
    bool emit_mlir = false;
    bool emit_llvm = false;
    bool emit_llvm_opt = false;                  // --emit-llvm-opt: LLVM IR AFTER the opt pipeline (honors -O)
    bool debug_g   = false;                      // -g / --debug: emit DWARF debug info (line tables, subprograms, locals, types) for gdb/lldb
    bool expand_only = false;                    // --expand: run metaprog dispatch over input + render result back to Logos source (no codegen). Avoids stdlib build's circular-dep when derives reference each other (debt #22 alt B).
    bool test_mode   = false;                    // --test: build a test binary (synthesise main() that runs every `#[test]` fn under panic-recovery and prints a Rust-style summary).
    bool stats_flag  = false;                    // --stats: print per-phase compile-time summary at end (also turns on inline phase trace).
    const char* emit_module_manifest = nullptr;  // --emit-module <manifest>
    bool        emit_abi_flag = false;            // --emit-abi: dump ABI surface spec
    bool        emit_docs_flag = false;           // --emit-docs: modifier on --emit-module; also writes <out>.docwr doc facts (ADR 0014)
    bool        print_prefix  = false;            // --print-prefix:  this version's tree root
    bool        print_lib_dir = false;            // --print-lib-dir: this version's stdlib dir
    bool        print_metadata = false;           // --print-metadata: all of the above as a Writ doc
    const char* abi_diff_old = nullptr;           // --abi-diff <old> <new>: qualify ABI change
    const char* abi_diff_new = nullptr;
    std::string only_file;                       // --only-file <path>: per-file emit (B1.7)
    std::string dump_metaprog_dir;                // --dump-metaprog <dir>: write metafn-emitted ASTs as Logos source under <dir>/<callee>__<file>_<line>/post_quote.logos
    std::string dump_metaprog_filter;             // --dump-metaprog-filter <pat>: comma-separated patterns (`*` / callee substring / `file:line`); empty or `*` = match all
    std::vector<std::string> cfg_flags;            // --cfg flag=val | --cfg flag — Phase 2-4 cfg/feature flags
    std::vector<std::string> search_paths;
    // Subset of search_paths populated only from explicit -L / --libs.
    // Used to scope `binary_symbols` collection: only user-explicit
    // module-library paths feed the body-skip set, so bare-name fns in
    // the system stdlib never shadow user-source definitions in projects
    // that don't opt in to stdlib's pre-baked bodies. The system module
    // path is still resolved into search_paths for module discovery —
    // only the symbol-skip side is restricted.
    std::vector<std::string> explicit_lib_paths;
    // Single-archive flags `-l FILE` / `--lib FILE`. These also feed
    // body-skip (user-explicit linkage intent) and are passed to the
    // module loader as additional binary modules.
    std::vector<std::string> explicit_lib_files;
    int opt_level = 0;
    bool overflow_checks = true;   // -C overflow-checks=off → wrapping int arith
    std::string target_cpu = "generic";  // -C target-cpu=native → host CPU (AVX…)
    bool no_system = false;
    bool print_system_libdir = false;
    bool print_version = false;

    // System module library discovery — argv[0]-relative with env override.
    //
    //   priority (highest first):
    //     1. CLI -L / -l       (user explicit; -L pushes here, -l later)
    //     2. LOGOS_LIB_DIR env (override on system path)
    //     3. argv[0]-relative LOGOS_LIB_RELDIR (default; baked at build)
    //
    // The result is appended to `search_paths` after CLI parsing so user
    // -L flags still take precedence over the system path during module
    // resolution.
    // Directory holding this binary, resolved through /proc/self/exe (follows
    // the alternatives/versioned symlink chain to the real file in the tree).
    auto exe_dir = []() -> std::string {
        char exe[PATH_MAX];
        ssize_t n = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (n <= 0) return {};
        exe[n] = '\0';
        std::string d(exe);
        if (auto slash = d.rfind('/'); slash != std::string::npos) d.resize(slash);
        return d;
    };
    auto resolve_system_lib_dir = [&exe_dir]() -> std::string {
        if (const char* env = std::getenv("LOGOS_LIB_DIR")) return env;
        std::string dir = exe_dir();
        if (dir.empty()) return {};
        // Candidate stdlib dirs by layout, each validated by the presence of a
        // stdlib archive so a bare dir (e.g. build/lib) is never mistaken for it:
        //   ../lib        — installed tree: binary at <tree>/bin, stdlib at <tree>/lib
        //   ../lib/logos  — flat build tree: build/bin/logosc, build/lib/logos
        for (const char* suffix : {"/../lib", "/../lib/logos"}) {
            char real[PATH_MAX];
            std::string cand = dir + suffix;
            if (::realpath(cand.c_str(), real)) {
                std::string probe = std::string(real) + "/liblogos-lang.a";
                if (::access(probe.c_str(), F_OK) == 0) return real;
            }
        }
        return {};
    };

    // Seed search paths from LOGOS_MODULE_PATH (colon-separated).
    if (const char* module_path_env = std::getenv("LOGOS_MODULE_PATH")) {
        std::string env_val(module_path_env);
        size_t start = 0;
        while (start < env_val.size()) {
            auto colon = env_val.find(':', start);
            if (colon == std::string::npos) colon = env_val.size();
            if (colon > start) search_paths.push_back(env_val.substr(start, colon - start));
            start = colon + 1;
        }
    }

    std::vector<std::string> dashI_paths;  // for compile-mode rejection
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) { output_path = argv[++i]; }
        else if (arg == "-I" && i + 1 < argc) {
            dashI_paths.push_back(argv[++i]);
            search_paths.push_back(dashI_paths.back());
        }
        else if ((arg == "-L" || arg == "--libs") && i + 1 < argc) {
            search_paths.push_back(argv[i+1]);
            explicit_lib_paths.push_back(argv[i+1]);
            ++i;
        }
        else if ((arg == "-l" || arg == "--lib") && i + 1 < argc) {
            explicit_lib_files.push_back(argv[++i]);
        }
        else if (arg == "--no-system") { no_system = true; }
        else if (arg == "--print-system-libdir") { print_system_libdir = true; }
        else if (arg == "--version" || arg == "-V") { print_version = true; }
        else if (arg == "--help" || arg == "-h") { print_usage(stdout); return 0; }
        else if (arg.rfind("--diag-format=", 0) == 0) {
            std::string_view fmt = arg;
            fmt.remove_prefix(14);
            if (fmt == "text") {
                logos::compiler::diag_format_global() = logos::compiler::DiagFormat::Text;
            } else if (fmt == "json") {
                logos::compiler::diag_format_global() = logos::compiler::DiagFormat::Json;
            } else {
                std::fprintf(stderr, "logosc: --diag-format must be 'text' or 'json'\n");
                return EXIT_USAGE;
            }
        }
        else if (arg == "--diag-format" && i + 1 < argc) {
            std::string_view fmt = argv[++i];
            if (fmt == "text") {
                logos::compiler::diag_format_global() = logos::compiler::DiagFormat::Text;
            } else if (fmt == "json") {
                logos::compiler::diag_format_global() = logos::compiler::DiagFormat::Json;
            } else {
                std::fprintf(stderr, "logosc: --diag-format must be 'text' or 'json'\n");
                return EXIT_USAGE;
            }
        }
        else if (arg == "--stats") { stats_flag = true; }
        else if (arg == "--emit-mlir") { emit_mlir = true; }
        else if (arg == "--emit-llvm") { emit_llvm = true; }
        else if (arg == "--emit-llvm-opt") { emit_llvm_opt = true; }
        else if (arg == "-g" || arg == "--debug") { debug_g = true; g_gen_debug_info = true; }
        else if (arg == "--expand") { expand_only = true; }
        else if (arg == "--emit-module" && i + 1 < argc) { emit_module_manifest = argv[++i]; }
        else if (arg == "--emit-abi") { emit_abi_flag = true; }
        else if (arg == "--gen-dir" && i + 1 < argc) { g_gen_dir = argv[++i]; }
        else if (arg.rfind("--gen-dir=", 0) == 0) {
            g_gen_dir = std::string(arg.substr(std::string("--gen-dir=").size()));
        }
        else if (arg == "--emit-docs") { emit_docs_flag = true; }
        else if (arg == "--print-prefix")  { print_prefix  = true; }
        else if (arg == "--print-lib-dir") { print_lib_dir = true; }
        else if (arg == "--print-metadata") { print_metadata = true; }
        else if (arg == "--abi-diff" && i + 2 < argc) { abi_diff_old = argv[++i]; abi_diff_new = argv[++i]; }
        else if (arg.rfind("--dump-metaprog=", 0) == 0) {
            std::string_view v = arg;
            v.remove_prefix(16);
            dump_metaprog_dir = std::string(v);
        }
        else if (arg == "--dump-metaprog" && i + 1 < argc) {
            dump_metaprog_dir = argv[++i];
        }
        else if (arg.rfind("--dump-metaprog-filter=", 0) == 0) {
            std::string_view v = arg;
            v.remove_prefix(23);
            dump_metaprog_filter = std::string(v);
        }
        else if (arg == "--dump-metaprog-filter" && i + 1 < argc) {
            dump_metaprog_filter = argv[++i];
        }
        else if (arg.rfind("--only-file=", 0) == 0) {
            std::string_view v = arg;
            v.remove_prefix(12);
            only_file = std::string(v);
        }
        else if (arg == "--only-file" && i + 1 < argc) {
            only_file = argv[++i];
        }
        else if (arg == "-O0") { opt_level = 0; }
        else if (arg == "-O1") { opt_level = 1; }
        else if (arg == "-O2") { opt_level = 2; }
        else if (arg == "-O3") { opt_level = 3; }
        // Overflow-check policy (rustc-style). Default ON (Logos safety-first).
        // `-C overflow-checks=off` (two-token, like rustc) or the convenience
        // `--overflow-checks=off` lowers int +/-/* to wrapping arith (no trap),
        // which vectorizes — at the cost of silent wraparound. `=on` is the default.
        else if (arg == "-C" && i + 1 < argc &&
                 std::string_view(argv[i + 1]).substr(0, 16) == "overflow-checks=") {
            std::string_view v = std::string_view(argv[++i]).substr(16);
            overflow_checks = !(v == "off" || v == "no" || v == "0" || v == "false");
        }
        else if (std::string_view(arg).substr(0, 18) == "--overflow-checks=") {
            std::string_view v = std::string_view(arg).substr(18);
            overflow_checks = !(v == "off" || v == "no" || v == "0" || v == "false");
        }
        // Backend target CPU (rustc-style). `-C target-cpu=native` (two-token) or
        // `--target-cpu=native` → host CPU (enables AVX/AVX2/AVX-512; non-portable).
        // Default "generic" = portable x86-64 baseline.
        else if (arg == "-C" && i + 1 < argc &&
                 std::string_view(argv[i + 1]).substr(0, 11) == "target-cpu=") {
            target_cpu = std::string(std::string_view(argv[++i]).substr(11));
        }
        else if (std::string_view(arg).substr(0, 13) == "--target-cpu=") {
            target_cpu = std::string(std::string_view(arg).substr(13));
        }
        // Phase 2-4: cfg feature flags. `--cfg feature=foo` sets feature
        // `foo`; multiple flags accumulate. Also accepts bare flag form
        // `--cfg key_or_bare_name` (currently no-op — cfg-key flags like
        // `unix` come from compile-target metadata, not user args). The
        // form `--cfg key=value` (non-feature) is reserved for future
        // use (e.g. `--cfg target_pointer_width=32` cross-compilation).
        else if (arg == "--cfg" && i + 1 < argc) {
            cfg_flags.push_back(argv[++i]);
        }
        else if (arg.rfind("--cfg=", 0) == 0) {
            cfg_flags.push_back(std::string(arg).substr(6));
        }
        else if (arg == "--test") { test_mode = true; }
        else if (arg[0] != '-' && !input_path) { input_path = argv[i]; }
    }

    if (print_system_libdir) {
        auto sys = resolve_system_lib_dir();
        std::printf("%s\n", sys.c_str());
        return 0;
    }

    // Resource locator (foundation for per-version docs/resources for AI agents):
    //   --print-lib-dir  this version's stdlib dir (linker/consumer search path;
    //                     alias of --print-system-libdir)
    //   --print-prefix   this version's tree root  (<...>/lib/logos/<SLOT>)
    // The binary self-locates via /proc/self/exe, so these answer "where are this
    // exact version's files" regardless of how it was invoked (versioned symlink,
    // alternatives, or the build tree).
    if (print_lib_dir) {
        std::string d = resolve_system_lib_dir();
        if (d.empty()) { std::fprintf(stderr, "logosc: --print-lib-dir: stdlib not found\n"); return 1; }
        std::printf("%s\n", d.c_str());
        return 0;
    }
    if (print_prefix) {
        char real[PATH_MAX];
        std::string root = exe_dir() + "/..";   // <tree>/bin/.. = <tree>
        if (!::realpath(root.c_str(), real)) { std::fprintf(stderr, "logosc: --print-prefix: cannot resolve\n"); return 1; }
        std::printf("%s\n", real);
        return 0;
    }
    // The general "give me all your metadata" query: a Writ-SDN document (the
    // same text form lforge parses for lforge.writ). lforge queries this once
    // — especially when driving a DIFFERENT-version logosc, where it cannot assume
    // the layout. Extensible: add fields without breaking parsers.
    if (print_metadata) {
        std::string lib = resolve_system_lib_dir();
        char real[PATH_MAX];
        std::string prefix = ::realpath((exe_dir() + "/..").c_str(), real) ? std::string(real) : std::string{};
        auto esc = [](const std::string& s) {
            std::string o; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o;
        };
#ifdef LOGOS_VERSION_FULL
        const char* ver = LOGOS_VERSION_FULL; const char* slot = LOGOS_VERSION_SLOT;
#else
        const char* ver = "0.1.0"; const char* slot = "0.1";
#endif
        std::printf("{\n");
        std::printf("    version: \"%s\",\n", esc(ver).c_str());
        std::printf("    slot:    \"%s\",\n", esc(slot).c_str());
        std::printf("    prefix:  \"%s\",\n", esc(prefix).c_str());
        std::printf("    lib_dir: \"%s\"\n",  esc(lib).c_str());
        std::printf("}\n");
        return 0;
    }

    if (print_version) {
        // Version is baked from CMake (LOGOS_VERSION_FULL = X.Y.Z[-pre]),
        // the single source of truth. lforge's `requires_logos` floor (B5)
        // compares against this segment-by-segment (the -pre tail is ignored
        // by its numeric cmp_tags).
#ifdef LOGOS_VERSION_FULL
        std::printf("logosc %s\n", LOGOS_VERSION_FULL);
#else
        std::printf("logosc 0.1.0\n");
#endif
        return 0;
    }

    // -I is only valid alongside --emit-module (where it drives source-file
    // discovery for the manifest). In normal compile mode source-level
    // imports don't exist; -L/-l carry binary modules instead.
    if (!emit_module_manifest && !dashI_paths.empty()) {
        std::fprintf(stderr,
            "logosc: error: -I '%s' is not allowed in compile mode "
            "(no source-level imports in Logos);\n"
            "  use -L DIR for binary module directories or -l FILE "
            "for a specific archive.\n"
            "  -I is only available with --emit-module.\n",
            dashI_paths.front().c_str());
        return 2;
    }

    // Append system module library to search_paths after CLI flags so
    // user-provided -L wins (search runs front-to-back). --no-system opts
    // out for hermetic / cross-compiling builds.
    if (!no_system) {
        auto sys = resolve_system_lib_dir();
        if (!sys.empty()) search_paths.push_back(sys);
    }

    // ── emit-abi mode ───────────────────────────────────────────────
    // Dump the binary-ABI surface of the stdlib on the search path (point -L at
    // a build/install lib dir, or rely on the resolved system path). Writes to
    // -o or stdout.
    if (emit_abi_flag) {
        // Default output_path "output.o" means "no -o given" → stdout.
        std::string abi_out = (std::string(output_path) == "output.o") ? std::string{}
                                                                       : std::string(output_path);
        return emit_abi_spec(search_paths, explicit_lib_files, abi_out);
    }
    if (abi_diff_old && abi_diff_new)
        return abi_diff(abi_diff_old, abi_diff_new);

    // ── emit-module mode ────────────────────────────────────────────
    if (emit_module_manifest) {
        std::string err;
        auto manifest = logos::compiler::parse_module_manifest(emit_module_manifest, err);
        if (!manifest) {
            std::fprintf(stderr, "logosc: %s\n", err.c_str());
            return EXIT_USAGE;
        }
        logos::compiler::EmitModuleOptions mopts;
        mopts.extra_search_paths = search_paths;
        mopts.emit_mlir = emit_mlir;
        mopts.emit_llvm = emit_llvm;
        mopts.emit_docs = emit_docs_flag;
        mopts.only_file = only_file;
        mopts.extra_lib_files = explicit_lib_files;
        mopts.opt_level = opt_level;
        mopts.overflow_checks = overflow_checks;
        mopts.target_cpu = target_cpu;
        return logos::compiler::emit_module(*manifest, output_path, mopts) ? 0 : 1;
    }

    // No input file (e.g. only unknown/typo'd flags, or a flag with no source):
    // print usage instead of feeding nullptr into the loader. Unknown dash-args
    // are otherwise silently ignored by the parse loop above.
    if (!input_path) {
        print_usage(stderr);
        return EXIT_USAGE;
    }

    const bool trace = std::getenv("LOGOS_TRACE_PHASES") != nullptr;
    logos::compiler::CompileStats top_stats;
    auto t_start = std::chrono::steady_clock::now();
    auto t_compile_start = t_start;
    auto report = [&](const char* label) {
        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - t_start).count();
        if (stats_flag) top_stats.add(label, ms, -1);
        if (trace) std::fprintf(stderr, "[trace %6lldms] %s\n", (long long)ms, label);
        t_start = now;
    };
    // Default-on implicit prelude. A normal compile injects `use
    // logos.std.prelude;` (transitively re-exporting the lang + mem preludes:
    // Option/Result/Some/None/Box/String/Vec + the core traits) into every
    // non-binary user file that doesn't opt out with `#![no_implicit_prelude]`.
    // `--no-system` (freestanding, no stdlib on the search path) disables it,
    // since the prelude package wouldn't be resolvable. Per-tier prelude
    // selection (lang/mem vs std) via a module manifest is a follow-up; the
    // stdlib's own library build runs through emit_module, which never sets
    // this, so it stays opted out and free of import cycles.
    std::string implicit_prelude_pkg = no_system ? std::string{} : "logos.std.prelude";

    // ── Step 1-2: Load and parse all modules ────────────────────
    bool loader_had_error = false;
    auto modules = logos::compiler::load_modules(input_path, search_paths, &loader_had_error, explicit_lib_files, implicit_prelude_pkg);
    report("load+parse");
    if (modules.empty()) {
        std::fprintf(stderr, "logosc: no modules loaded\n");
        return EXIT_LINK_IO;
    }
    if (loader_had_error) {
        // B-mv-03/04: `use <pkg>;` referencing a missing package is fatal.
        // The loader already emitted "module_loader: cannot find package 'X'".
        return EXIT_LINK_IO;
    }

    // Collect ASTs and source paths.
    std::vector<logos::writ::Writ> asts;
    std::vector<std::string> filenames;
    std::vector<bool> from_binary;
    std::vector<bool> is_lazy;
    std::vector<std::string> module_ids;   // parallel to asts (owning-module mangle key)
    // §3: module canonical NAME → mangle id, for resolving `use pkg from <name>`.
    std::unordered_map<std::string, std::string> module_name_to_id;
    logos::compiler::StrSet binary_archives_seen;
    logos::compiler::StrSet binary_symbols;
    for (auto& m : modules) {
        filenames.push_back(m.path);
        // Phase 6 (multi-arena IR): lazy modules came from a binary archive
        // (no source dir scan) but ship parsed AST only — no .o text, no
        // LIR blob. Treat them as non-binary for sema so bodies get
        // lowered locally into the consumer's LProgram (same path as
        // user-authored code). Stamp is_lazy[] in parallel so sema can
        // tag LFunction.from_lazy_module for post-mono reach filtering.
        from_binary.push_back(m.from_binary_module && !m.is_lazy);
        is_lazy.push_back(m.is_lazy);
        module_ids.push_back(m.module_id);   // empty for the user program's own files; set for binary modules
        if (!m.module_name.empty() && !m.module_id.empty())
            module_name_to_id.emplace(m.module_name, m.module_id);  // §3: name→id
        asts.push_back(std::move(m.ast));
    }
    // Collect symbol tables from binary archives on the search path.
    // Shell glob + nm: avoids pulling in <filesystem> next to LLVM headers.
    // M.1 Stage 2 (Mode B): also collect the archive paths themselves so the
    // metaprog JIT can register them via StaticLibraryDefinitionGenerator.
    std::vector<std::string> archive_paths;
    // archive_paths spans every search dir (incl. system) plus -l files
    // so the metacall ORC JIT can resolve every available symbol at
    // compile time. Native fiber-context archives carry TLS relocations
    // that ORC's RuntimeDyld can't handle (R_X86_64_GOTTPOFF), so we
    // filter `*_fibers.a` here. Those symbols are still resolved at
    // user-link time — the JIT just doesn't need them, since metaprog
    // handlers run in the host's main thread, not in a Logos fiber.
    auto is_jit_unsafe_archive = [](std::string_view sv) {
        auto last = sv.rfind('/');
        std::string_view base = (last == std::string_view::npos) ? sv : sv.substr(last + 1);
        // Native fiber-context archive carries TLS relocations ORC's
        // RuntimeDyld can't handle (R_X86_64_GOTTPOFF). Metaprog hooks run on
        // the host main thread, not a Logos fiber, so the JIT doesn't need it.
        if (base.find("_fibers.a") != std::string_view::npos) return true;
        // Modular stdlib: each layer's .o now carries ONLY its own codegen
        // (the layer-dedup in emit_module forward-declares whatever a `depends`
        // archive already defines). So the three layers are DISJOINT — the
        // metacall JIT loads all of liblogos-{lang,mem,std}.a and ORC resolves
        // cross-layer refs without duplicate-definition conflicts. (Previously
        // std.a was self-contained and the lower layers were filtered as
        // redundant duplicates.)
        return false;
    };
    for (const auto& dir : search_paths) {
        std::string cmd = "ls " + dir + "/*.a 2>/dev/null";
        if (FILE* lp = ::popen(cmd.c_str(), "r")) {
            char path[1024];
            while (std::fgets(path, sizeof(path), lp)) {
                std::string_view sv(path);
                while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r' || sv.back() == ' '))
                    sv.remove_suffix(1);
                if (!sv.empty() && !is_jit_unsafe_archive(sv))
                    archive_paths.emplace_back(sv);
            }
            ::pclose(lp);
        }
    }
    for (const auto& f : explicit_lib_files) {
        if (!is_jit_unsafe_archive(f)) archive_paths.push_back(f);
    }

    // M3 step 3: merge .writ0 v3 exports trailers from every archive on
    // the link path. The result is name-only stdlib template catalog; future
    // M3 steps extend the trailer with mirror offsets so mono can short-
    // circuit indexing/instantiation work. For now mono just stores it.
    auto stdlib_exports = logos::compiler::load_archive_exports(archive_paths);
    if (std::getenv("LOGOS_TRACE_PHASES")) {
        std::fprintf(stderr,
            "[trace] stdlib_exports: %zu struct, %zu enum, %zu fn templates, %zu blanket, %zu concrete impls from %zu archive(s)\n",
            stdlib_exports.struct_templates.size(),
            stdlib_exports.enum_templates.size(),
            stdlib_exports.fn_templates.size(),
            stdlib_exports.blanket_impls.size(),
            stdlib_exports.concrete_impls.size(),
            archive_paths.size());
    }
    // G156-1: dependency-archive nominal decls (pkg,name) for the ambiguity
    // universe — so a user compile folds a cross-module same-name type
    // (fs.DirEntry) identically to how its owning stdlib archive folded it.
    std::vector<std::pair<std::string, std::string>> dep_nominal_decls;
    dep_nominal_decls.reserve(stdlib_exports.all_struct_decls.size() +
                              stdlib_exports.all_enum_decls.size());
    for (auto& pn : stdlib_exports.all_struct_decls) dep_nominal_decls.push_back(pn);
    for (auto& pn : stdlib_exports.all_enum_decls)   dep_nominal_decls.push_back(pn);

    // binary_symbols: mlir_gen consults this set to skip body emission for
    // fns whose pre-baked implementation is already in an archive on the
    // search path. Emitting them again duplicates work (multiply-define
    // would happen too, but the linker resolves it; the cost is mlir_gen
    // time on stdlib fns that are already in liblstdlib.a). We now collect
    // from BOTH user-explicit -L/-l AND the system stdlib (LOGOS_LIB_DIR /
    // implicit search paths) — set LOGOS_NO_LIB_BINARY_SKIP=1 to disable
    // the system-stdlib half if a project wants to override stdlib symbols.
    auto collect_syms = [&](const std::string& cmd) {
        FILE* pipe = ::popen(cmd.c_str(), "r");
        if (!pipe) return;
        char line[512];
        while (std::fgets(line, sizeof(line), pipe)) {
            std::string_view sv(line);
            while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r' || sv.back() == ' '))
                sv.remove_suffix(1);
            if (!sv.empty() && sv.front() != '/') {
                binary_symbols.emplace(sv);
                // Module system: a METHOD symbol is emitted module-qualified as
                // `<module>..<pkg>.<rest>` (mlir-gen link_name), but mono's
                // precompiled-skip checks use the BARE `<pkg>.<rest>` form
                // (function_symbol_name exempts methods from the module segment).
                // Insert the bare alias (everything after the `..` sentinel) so a
                // stdlib method is recognised as already compiled — else mono
                // re-monomorphises the ENTIRE stdlib in every consumer compile
                // (mono+borrow re-instantiation storm: borrow 12ms→1300ms). Free
                // fns use a single `.`/`$` boundary (no `..`) and are unaffected.
                // No bare aliases: sema's skel_skip_body now tests the
                // QUALIFIED `<module_id>..` form directly (lockstep with
                // mlir_gen's is_binary_skip). The old alias bridge both
                // desynced the two gates (gap #2) and, once `$M`-guarded,
                // matched nothing — disabling skeleton-skip entirely.
            }
        }
        ::pclose(pipe);
    };
    // `-p` (no-sort): we only build a set, so nm's default symbol sort is pure
    // waste — and it sorts via locale-aware strcoll (the #1 cost when compiling a
    // small stdlib consumer, ~9% of wall-clock, thousands of stdlib symbols).
    for (const auto& dir : explicit_lib_paths)
        collect_syms("nm --defined-only -p -j " + dir + "/*.a 2>/dev/null");
    for (const auto& f : explicit_lib_files)
        collect_syms("nm --defined-only -p -j " + f + " 2>/dev/null");
    if (!std::getenv("LOGOS_NO_LIB_BINARY_SKIP")) {
        for (const auto& dir : search_paths)
            collect_syms("nm --defined-only -p -j " + dir + "/*.a 2>/dev/null");
    }
    if (trace)
        std::fprintf(stderr, "[trace] binary_symbols: %zu from %zu archive(s)\n",
                     binary_symbols.size(), binary_archives_seen.size());

    // ── Step 2a.5: --test pre-scan ─────────────────────────────────────
    // Walk the user-provided asts to capture #[test] / #[should_panic] /
    // #[ignore] annotations + validate user_has_main + capture entry_pkg.
    // Runner synthesis is DEFERRED until after the metacall splice loop
    // converges — that's the only way to pick up #[test] fns emitted by
    // item-position macros (`int_module!{...}`). The post-sema augment
    // pass below walks prog.functions for is_test and merges with the
    // ast-level list (de-duped by name).
    struct TestEntry {
        std::string name;
        bool        should_panic = false;
        bool        ignored      = false;
        // TH-th-02: `#[should_panic(expected = "msg")]`. Empty when no
        // expected arg supplied — matches Rust semantics (any panic
        // accepted). For ast-level tests this is captured at parse from
        // the annotation args; for post-sema-discovered tests it comes
        // from LFunction::should_panic_expected_msg.
        std::string expected_msg;
    };
    std::vector<TestEntry> tests;
    std::string entry_pkg;
    bool user_has_main = false;

    if (test_mode) {
        namespace la = logos::compiler::ast;
        using logos::writ::AnyVal;
        using logos::writ::TinyMapView;
        using logos::writ::ArrayView;
        using logos::writ::StringView;

        auto entry_idx = asts.empty() ? 0 : asts.size() - 1;
        // Helper: compute the dotted package name of a parsed module AST.
        auto pkg_of = [&](size_t i) -> std::string {
            if (from_binary[i]) return {};
            auto* holder = asts[i].holder();
            if (!holder) return {};
            auto root = asts[i].root_object().as_tiny_map();
            auto code_v = root.get(la::CODE.code);
            int32_t rcode = code_v.is_null() ? -1 : code_v.as_value<int32_t>();
            if (rcode != la::MODULE.code) return {};
            std::string pkg;
            if (root.has_key(la::NAME)) {
                auto nm_av = root.get(la::NAME.code);
                if (!nm_av.is_null())
                    pkg = std::string(StringView(nm_av, holder).view());
            }
            if (root.has_key(la::mod::PATH_PARTS)) {
                auto parts_av = root.get(la::mod::PATH_PARTS.code);
                if (!parts_av.is_null()) {
                    ArrayView parts(parts_av, holder);
                    for (uint64_t k = 0; k < parts.size(); ++k) {
                        TinyMapView p(parts.get(k), holder);
                        if (!p.has_key(la::NAME)) continue;
                        auto pn = p.get(la::NAME.code);
                        if (pn.is_null()) continue;
                        if (!pkg.empty()) pkg.push_back('.');
                        pkg += std::string(StringView(pn, holder).view());
                    }
                }
            }
            return pkg;
        };
        // Pass 1: resolve entry package from the entry file.
        entry_pkg = pkg_of(entry_idx);
        // Pass 2: collect #[test] fns from every file whose package matches.
        // TH-th-01: previously restricted to the entry file; now any module
        // in the same dotted package counts as part of the test surface.
        for (size_t i = 0; i < asts.size(); ++i) {
            if (from_binary[i]) continue;
            auto* holder = asts[i].holder();
            if (!holder) continue;
            auto root = asts[i].root_object().as_tiny_map();
            auto code_v = root.get(la::CODE.code);
            int32_t rcode = code_v.is_null() ? -1 : code_v.as_value<int32_t>();
            if (rcode != la::MODULE.code) continue;
            std::string pkg = pkg_of(i);
            bool same_pkg = (!entry_pkg.empty() && pkg == entry_pkg);
            if (!root.has_key(la::ITEMS)) continue;
            ArrayView items(root.get(la::ITEMS.code), holder);
            bool pending_test = false, pending_sp = false, pending_ig = false;
            std::string pending_expected;
            for (uint64_t j = 0; j < items.size(); ++j) {
                TinyMapView item(items.get(j), holder);
                auto cv = item.get(la::CODE.code);
                int32_t ic = cv.is_null() ? -1 : cv.as_value<int32_t>();
                if (ic == la::ANNOTATION.code) {
                    if (item.has_key(la::NAME)) {
                        auto nv = item.get(la::NAME.code);
                        std::string_view nm = nv.is_null()
                            ? std::string_view{}
                            : StringView(nv, holder).view();
                        if      (nm == "test")         pending_test = true;
                        else if (nm == "should_panic") {
                            pending_sp = true;
                            // TH-th-02: extract `expected = "..."` named arg
                            // if present (annotation grammar puts it in
                            // ARGS as an ANNOT_KV with NAME=expected,
                            // VALUE=LIT_STR).
                            if (item.has_key(la::ARGS)) {
                                auto args_av = item.get(la::ARGS.code);
                                if (!args_av.is_null()) {
                                    TinyMapView args_map(args_av, holder);
                                    if (args_map.has_key(la::ITEMS)) {
                                        ArrayView items_arr(
                                            args_map.get(la::ITEMS.code),
                                            holder);
                                        for (uint64_t k = 0; k < items_arr.size(); ++k) {
                                            TinyMapView a(items_arr.get(k), holder);
                                            auto ac = a.get(la::CODE.code);
                                            if (ac.is_null()) continue;
                                            if (ac.as_value<int32_t>() != la::ANNOT_KV.code)
                                                continue;
                                            if (!a.has_key(la::NAME) || !a.has_key(la::VALUE))
                                                continue;
                                            auto kn = a.get(la::NAME.code);
                                            std::string_view kname = kn.is_null()
                                                ? std::string_view{}
                                                : StringView(kn, holder).view();
                                            if (kname != "expected") continue;
                                            TinyMapView v(a.get(la::VALUE.code),
                                                          holder);
                                            auto vc = v.get(la::CODE.code);
                                            if (vc.is_null()) continue;
                                            if (vc.as_value<int32_t>() != la::LIT_STR.code)
                                                continue;
                                            auto raw_av = v.get(la::VALUE.code);
                                            if (raw_av.is_null()) continue;
                                            std::string_view raw = StringView(
                                                raw_av, holder).view();
                                            // raw is "..." — strip surrounding quotes.
                                            if (raw.size() >= 2
                                                && raw.front() == '"'
                                                && raw.back() == '"') {
                                                pending_expected.assign(
                                                    raw.substr(1, raw.size() - 2));
                                            }
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        else if (nm == "ignore")       pending_ig   = true;
                    }
                    continue;
                }
                if (ic == la::DOC_LINE_LIT.code || ic == la::INNER_DOC_LIT.code)
                    continue;
                if ((ic == la::FN.code || ic == la::EXTERN_FN.code) &&
                    item.has_key(la::NAME)) {
                    auto nv = item.get(la::NAME.code);
                    std::string nm = nv.is_null()
                        ? std::string{}
                        : std::string(StringView(nv, holder).view());
                    if (nm == "main" && same_pkg) user_has_main = true;
                    if (pending_test && same_pkg) {
                        tests.push_back({nm, pending_sp, pending_ig,
                                         std::move(pending_expected)});
                    }
                }
                pending_test = pending_sp = pending_ig = false;
                pending_expected.clear();
            }
        }

        if (user_has_main) {
            std::fprintf(stderr,
                "logosc: --test: user `fn main()` conflicts with the synthesised "
                "test runner; remove it or compile without --test\n");
            return 1;
        }
        if (entry_pkg.empty()) {
            std::fprintf(stderr,
                "logosc: --test: entry file has no `package` declaration\n");
            return 1;
        }
        // Runner synthesis is DEFERRED to after the metacall splice loop
        // converges — see the post-sema augment + synth block below.
        // The "no tests found" check moves there too: tests emitted by
        // item-position macros (`int_module!{...}` etc.) only appear in
        // prog.functions, not in the pre-sema AST.
    }

    // ── Step 2b: Semantic analysis + L-IR lowering, with metaprog loop ──
    //
    // Discovery loop body lives in run_metaprog_dispatch (shared with
    // emit_module's stdlib-build path, debt #21 closure). Globals it
    // needs (g_asts, g_filenames, etc.) are set inside that function
    // and restored on return; we re-set them below for the post-loop
    // metacall-splice phase that still runs in main()'s scope.
    std::vector<std::optional<EmitProvenance>> ast_provenance;
    // Capture entry idx BEFORE dispatch — synth docs append to asts so
    // `asts.size()-1` no longer points at the user's input file after
    // dispatch returns.
    // --test mode used to splice the runner here too, so the
    // `<test_main_synth>` filename appeared at asts.back(); runner
    // synthesis is now deferred until after the metacall splice loop
    // converges so item-position macro-emitted `#[test]` fns are
    // visible. No back-step needed.
    size_t pre_dispatch_entry_idx = asts.empty() ? 0 : asts.size() - 1;

    // M5: one SemaCache shared by every sema_lower invocation in this
    // compile session. Holds the TypePool alive across the 5+ calls so
    // cached TypeRefs (Step 3b+) stay valid.
    logos::compiler::SemaCache sema_cache;

    // ONE dispatch configuration. Every point that re-enters the discovery
    // loop needs the same one, and it was written out three times identically
    // — a field added to one and not the others is a silent divergence between
    // phases that have to agree.
    auto dispatch_opts = [&] {
        logos::compiler::MetaprogDispatchOpts o;
        o.trace          = trace;
        o.dump_dir       = dump_metaprog_dir;
        o.dump_filter    = dump_metaprog_filter;
        o.archive_paths  = archive_paths;
        o.provenance_out = &ast_provenance;
        o.cfg_flags      = cfg_flags;                 // Phase 2-4
        o.binary_symbols = binary_symbols;
        o.dep_nominal_decls = dep_nominal_decls;      // G156-1 ambiguity universe
        o.stats_out      = stats_flag ? &top_stats : nullptr;
        o.sema_cache     = &sema_cache;
        o.implicit_prelude = implicit_prelude_pkg;
        o.module_ids     = &module_ids;  // module system: parallel to asts; grows with it
        o.self_module_id = "";           // a plain user program is in the global module (no id)
        o.module_name_to_id = module_name_to_id;      // §B-coex: `use … from` in discovery
        return o;
    };

    {
        auto mopts = dispatch_opts();
        if (logos::compiler::run_metaprog_dispatch(
                asts, filenames, from_binary, pre_dispatch_entry_idx, mopts) != 0)
            return 1;

        // ADR 0021 Phase 4b: the generic-container HARVEST is retired.
        // Its whole job — auto-generating a concrete b+tree family per
        // (K,V) for a `container Map<K,V>` used at a site — is now the
        // mono factory-demand seam (CtrClass<CFG> → __container_factory,
        // keyed by @hs(CFG)). Proven dead: pending_container_srcs was filled
        // ZERO times across the full 6647-test suite.
    }

    // --expand mode: render the entry-file AST + any synth docs that
    // were appended during dispatch as a single Logos source file
    // (debt #22 alt B — pre-expansion). Skips sema/mono/mlir-gen/link
    // entirely. Output goes to `output_path` (-o flag, default "output.o"
    // — typically overridden to "<file>.expanded.logos" by the caller).
    if (expand_only) {
        size_t entry_idx = pre_dispatch_entry_idx;
        // Collect registered metaprog-handler trigger names so we can
        // strip the originating `#[trigger]` annotations from rendered
        // output — they've been consumed by dispatch and re-firing them
        // on the next compile would duplicate emitted items.
        std::set<std::string> consumed_triggers;
        {
            auto post_prog = logos::compiler::sema_lower(asts, filenames, from_binary,
                                                          logos::compiler::SemaOptions{}, is_lazy, module_ids);
            for (auto& mh : post_prog.metaprog_handlers) {
                if (mh.trigger() != "<missing>")
                    consumed_triggers.insert(std::string(mh.trigger()));
            }
        }
        // Helper: split a rendered module string into:
        //   header  — `package ...;` line
        //   uses    — vector of `use ...;` lines (no semicolon stripping)
        //   body    — everything else (items)
        auto split_rendered = [](const std::string& s, std::string& header,
                                  std::vector<std::string>& uses,
                                  std::string& body) {
            header.clear(); uses.clear(); body.clear();
            std::string_view v = s;
            size_t pos = 0;
            bool body_started = false;
            while (pos < v.size()) {
                size_t nl = v.find('\n', pos);
                size_t end = (nl == std::string_view::npos) ? v.size() : nl;
                std::string_view line = v.substr(pos, end - pos);
                auto first = line.find_first_not_of(" \t");
                bool blank = first == std::string_view::npos;
                bool is_pkg = !blank && line.substr(first).rfind("package ", 0) == 0;
                bool is_use = !blank && line.substr(first).rfind("use ", 0) == 0;
                if (!body_started) {
                    if (is_pkg) {
                        header.assign(v.data() + pos, end - pos);
                    } else if (is_use) {
                        uses.emplace_back(v.data() + pos, end - pos);
                    } else if (!blank) {
                        body_started = true;
                        body.append(v.data() + pos, v.size() - pos);
                        break;
                    }
                }
                if (nl == std::string_view::npos) break;
                pos = nl + 1;
            }
        };
        auto strip_consumed_annots = [&](std::string& body) {
            std::string out;
            std::string_view v = body;
            size_t pos = 0;
            while (pos < v.size()) {
                size_t nl = v.find('\n', pos);
                size_t end = (nl == std::string_view::npos) ? v.size() : nl;
                std::string_view line = v.substr(pos, end - pos);
                auto first = line.find_first_not_of(" \t");
                bool drop = false;
                if (first != std::string_view::npos &&
                    line.size() >= first + 2 &&
                    line[first] == '#' && line[first + 1] == '[') {
                    auto name_start = first + 2;
                    size_t name_end = name_start;
                    while (name_end < line.size()) {
                        char c = line[name_end];
                        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                              (c >= '0' && c <= '9') || c == '_')) break;
                        ++name_end;
                    }
                    std::string name(line.substr(name_start, name_end - name_start));
                    if (consumed_triggers.count(name)) drop = true;
                }
                if (!drop) {
                    out.append(v.data() + pos, end - pos);
                    if (nl != std::string_view::npos) out.push_back('\n');
                }
                if (nl == std::string_view::npos) break;
                pos = nl + 1;
            }
            body = std::move(out);
        };

        // Combine entry + synth docs. Header from entry; merged-deduped
        // USES from entry + every synth doc; bodies concatenated.
        std::string entry_header;
        std::vector<std::string> entry_uses;
        std::string entry_body;
        if (entry_idx < asts.size()) {
            auto* h = asts[entry_idx].holder();
            auto root_off = logos::writ::WritAccess::root_offset(asts[entry_idx]);
            std::string r = logos::compiler::render_module_source_for_dump(h, root_off);
            split_rendered(r, entry_header, entry_uses, entry_body);
            strip_consumed_annots(entry_body);
        }

        std::vector<std::string> all_uses = entry_uses;
        std::set<std::string> uses_seen(all_uses.begin(), all_uses.end());
        std::string synth_bodies;
        for (size_t i = 0; i < asts.size(); ++i) {
            if (i == entry_idx) continue;
            if (i >= filenames.size()) continue;
            const std::string& fn = filenames[i];
            if (fn != "<metaprog-blob-subst>" && fn != "<metaprog>" &&
                fn != "<metaprog-thunk>" && !fn.ends_with(".gen.logos"))
                continue;
            auto* h = asts[i].holder();
            auto root_off = logos::writ::WritAccess::root_offset(asts[i]);
            std::string r = logos::compiler::render_module_source_for_dump(h, root_off);
            std::string s_hdr, s_body;
            std::vector<std::string> s_uses;
            split_rendered(r, s_hdr, s_uses, s_body);
            for (auto& u : s_uses) {
                if (uses_seen.insert(u).second) all_uses.push_back(u);
            }
            synth_bodies += "\n// ── derive-expansion (was synth doc) ──\n";
            synth_bodies += s_body;
        }

        std::string out;
        if (!entry_header.empty()) { out += entry_header; out += "\n\n"; }
        for (auto& u : all_uses) { out += u; out += "\n"; }
        if (!all_uses.empty()) out += "\n";
        out += entry_body;
        out += synth_bodies;

        FILE* f = std::fopen(output_path, "w");
        if (!f) {
            std::fprintf(stderr, "logosc: --expand: cannot write %s\n", output_path);
            return EXIT_LINK_IO;
        }
        std::fwrite(out.data(), 1, out.size(), f);
        std::fclose(f);
        return EXIT_OK;
    }
    // Globals were restored on dispatch exit; re-establish them for
    // the post-loop metacall splice + dump-metaprog readback below.
    std::set<std::string> emit_seen;
    g_emit_seen     = &emit_seen;
    g_asts          = &asts;
    g_filenames     = &filenames;
    g_from_binary   = &from_binary;
    g_user_root_idx = asts.empty() ? 0 : asts.size() - 1;
    if (!dump_metaprog_dir.empty()) g_ast_provenance = &ast_provenance;
    logos::compiler::lir::LProgram prog;
    // Phase 7 slice 17: final, non-metaprog sema pass. Discovery loop ran in
    // metaprog_mode which skips entry-file fn bodies; here we lower them
    // for real, now that all hook-synthesized items are present.
    //
    // M.1 Stage 2: this sema pass is also where metacall sites are first
    // discovered (metaprog_mode skipped fn bodies, so METACALL was never
    // visited in the loop above). Sites carry synthesised thunk source;
    // we run a small inner loop here to compile + invoke + splice the
    // thunks, then re-run sema until no METACALL remains.
    // Phase 2-4: build a default SemaOptions carrying cfg_flags, shared
    // across the remaining sema_lower call sites in main().
    logos::compiler::SemaOptions default_opts;
    default_opts.cfg_flags      = cfg_flags;
    default_opts.cache          = &sema_cache;  // M5
    default_opts.binary_symbols = binary_symbols;  // skeleton-skip gate
    default_opts.dep_nominal_decls = dep_nominal_decls;  // G156-1 ambiguity universe
    default_opts.implicit_prelude = implicit_prelude_pkg;  // default-on prelude
    default_opts.module_name_to_id = module_name_to_id;    // §3: resolve `use … from <name>`
    prog = logos::compiler::sema_lower(asts, filenames, from_binary, default_opts, is_lazy, module_ids);

    // ── deem PLANS meet their families ──────────────────────────────────────
    // A `deem` whose source binds a container CLASS is not a function but a
    // plan: which code it becomes depends on the class TOGETHER WITH its type
    // arguments, and that pair first exists at a CALL — the argument's type is
    // the family the class became. Sema recorded those pairs; instantiate each
    // into an ordinary deem over the family handle and re-run.
    //
    // This sits before the diagnostics gate on purpose: until a plan is
    // instantiated the query fn does not exist, so the call to it and
    // everything downstream is a cascade, not a finding. The loop ends when
    // nothing new is emitted — and THEN the diagnostics are the truth.
    {
        constexpr int kMaxPlanIters = 8;
        bool plan_any_emitted = false;
        auto* saved_any = g_any_emitted;
        g_any_emitted = &plan_any_emitted;
        // ONE drain step, and the rules that make it correct live here rather
        // than at each caller:
        //
        //   · at most ONE kind of producer advances per step. The plan is
        //     written against the family handle, so the family must already be
        //     in the program when the plan's deem is lowered; rendering both in
        //     one step lowers that deem before the factory metacall has run.
        //   · what a producer emits is an ORDINARY item, so it is consumed by
        //     the metaprog dispatch — the window every user-level deem and
        //     metacall passes through. Re-enter it, then re-lower.
        //   · the dispatch restores its own emit globals on exit, so they are
        //     re-wired here, once, instead of at each caller that forgets.
        //
        // 1 = something was produced, 0 = nothing left to do, -1 = the
        // dispatch failed.
        auto drain_step = [&]() -> int {
            int n = render_factory_chunks(prog, g_early_drained_hashes);
            if (n == 0) n = render_deem_plan_chunks(prog, g_deem_plan_seen);
            if (n == 0) return 0;
            auto popts = dispatch_opts();
            if (logos::compiler::run_metaprog_dispatch(
                    asts, filenames, from_binary, pre_dispatch_entry_idx, popts) != 0)
                return -1;
            g_emit_seen = &emit_seen;
            g_asts = &asts; g_filenames = &filenames; g_from_binary = &from_binary;
            g_any_emitted = &plan_any_emitted;
            prog = logos::compiler::sema_lower(asts, filenames, from_binary,
                                               default_opts, is_lazy, module_ids);
            return 1;
        };
        // Runs to the worklist's IMMOBILITY — a step that produces nothing
        // ends it. The count is a backstop, not the termination rule.
        for (int pi = 0; pi < kMaxPlanIters; ++pi) {
            if (prog.deem_plan_insts.empty()) break;
            int r = drain_step();
            if (r < 0) return 1;
            if (r == 0) break;
        }
        g_any_emitted = saved_any;
    }
    prog.print_diags(stderr);
    if (!prog.ok()) return 1;

    {
        // logos_emit_source requires g_any_emitted alive; the metaprog loop's
        // local has gone out of scope by now, so wire up a fresh one for the
        // metacall splice phase.
        bool mc_any_emitted = false;
        g_any_emitted = &mc_any_emitted;
        constexpr int kMaxMetacallIters = 16;
        // Per-iteration timing helper (same shape as the metaprog dispatch
        // loop). Labels prefixed with "mc_" so the --stats summary keeps
        // metacall slices distinct from metaprog ones.
        auto mc_stat_step = [&](std::chrono::steady_clock::time_point& t,
                                const char* label, int iter_idx) {
            auto now = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - t).count();
            if (stats_flag) top_stats.add(std::string("mc_") + label, ms, iter_idx);
            t = now;
        };
        for (int mi = 0; !prog.metacall_sites.empty(); ++mi) {
            if (mi >= kMaxMetacallIters) {
                std::fprintf(stderr,
                    "logosc: metacall loop did not converge in %d iterations\n",
                    kMaxMetacallIters);
                return 1;
            }
            // Step 1: emit thunk sources for new sites (dedup via emit_seen).
            bool emitted_any_thunk = false;
            for (const auto& site : prog.metacall_sites) {
                if (site.thunk_source().empty()) continue;
                if (logos_emit_source_thunk(std::string(site.thunk_source()).c_str()))
                    emitted_any_thunk = true;
            }
            if (emitted_any_thunk) {
                // Re-sema so the JIT module below picks up the new thunks.
                auto _t_resema = std::chrono::steady_clock::now();
                prog = logos::compiler::sema_lower(asts, filenames, from_binary, default_opts, is_lazy, module_ids);
                mc_stat_step(_t_resema, "resema_after_emit", mi);
                prog.print_diags(stderr);
                if (!prog.ok()) return 1;
            }

            // Step 2: full pipeline through JIT for the metacall thunks.
            // Same trick as in the metaprog dispatcher: a fresh sema_lower
            // here would duplicate work already done above. Snapshot the
            // fields mono_pass discards (metacall_sites + macro_arg_blobs
            // are used later in this iter), then move-construct mc_prog
            // from `prog`.
            auto _mc_t = std::chrono::steady_clock::now();
            auto saved_metacall_sites = prog.metacall_sites;
            // Stage E: macro_arg_blobs is an arena-backed ObjectMap handle —
            // copy the handle (the map lives in prog.type_pool, whose arena is
            // PIMPL-stable across the move below) and reset prog's so the next
            // re-sema iter builds a fresh map.
            auto saved_macro_args     = prog.macro_arg_blobs;
            prog.macro_arg_blobs      = logos::compiler::lir_view::ObjectMapRef{};
            auto mc_prog = std::move(prog);
            mc_stat_step(_mc_t, "sema_lower", mi);
            if (!mc_prog.ok()) { mc_prog.print_diags(stderr); return 1; }
            // Same skip-already-compiled-stdlib trick as the metaprog JIT
            // and the final user-compile path. The metacall JIT registers
            // archive_paths via build_jit_from_module, so binary_symbols
            // covers everything mlir_gen would otherwise re-emit.
            for (auto& __s : binary_symbols) logos::compiler::lir_mirror_map_put_null(mc_prog, mc_prog.binary_symbols, __s);
            mc_prog = logos::compiler::reflection_emit(std::move(mc_prog));
            mc_stat_step(_mc_t, "reflection", mi);
            // Reachability prune: the metacall JIT only executes the thunks,
            // so monomorphize from those roots only. Without this, mono clones
            // the whole user program (test fns + their iterator
            // monomorphizations) the JIT never calls, and mlir_gen pays to
            // lower every dead body. The GlobalDCE below then drops them at the
            // LLVM level — far too late. Entry points = the `__metacall_thunk_*`
            // names; mono's scan cascade pulls in their (all-metaprog) closure.
            {
                // Prune only when EVERY site is an AST-returning macro
                // (ExprBlob/ItemBlob): those thunks call metaprog that
                // manipulates syntax trees, a self-contained closure we can
                // monomorphize from the thunk roots. A value-returning
                // expression metacall (`metacall build_cfg()` → WritStatic,
                // …) instead JIT-executes arbitrary USER runtime code whose
                // dynamic-dispatch deps (vtables, Writ TypeCode tag tables)
                // a static call-graph can't see — pruning there drops a
                // dispatch target and the JIT jumps to 0x0. Leave those eager.
                using MCRetTag = logos::compiler::lir::MetacallRetTag;
                bool all_macro = !saved_metacall_sites.empty();
                for (const auto& site : saved_metacall_sites)
                    if (site.ret_tag() != MCRetTag::ExprBlob &&
                        site.ret_tag() != MCRetTag::ItemBlob) { all_macro = false; break; }
                logos::compiler::MonoOpts mc_mopts;
                if (all_macro && !std::getenv("LOGOS_NO_MC_PRUNE"))
                    for (const auto& site : saved_metacall_sites)
                        if (!site.thunk_name().empty())
                            mc_mopts.entry_points.insert(std::string(site.thunk_name()));
                mc_prog = logos::compiler::mono_pass(std::move(mc_prog), std::move(mc_mopts));
            }
            mc_stat_step(_mc_t, "mono", mi);
            if (!mc_prog.ok()) { mc_prog.print_diags(stderr); return 1; }
            mc_prog = logos::compiler::borrow_check(std::move(mc_prog));
            mc_stat_step(_mc_t, "borrow", mi);
            if (!mc_prog.ok()) { mc_prog.print_diags(stderr); return 1; }

            mlir::MLIRContext mc_ctx;
            mc_ctx.getOrLoadDialect<mlir::func::FuncDialect>();
            mc_ctx.getOrLoadDialect<mlir::arith::ArithDialect>();
            mc_ctx.getOrLoadDialect<mlir::scf::SCFDialect>();
            mc_ctx.getOrLoadDialect<mlir::cf::ControlFlowDialect>();
            mc_ctx.getOrLoadDialect<mlir::LLVM::LLVMDialect>();
            auto mc_mlir = logos::compiler::mlir_gen(mc_ctx, mc_prog);
            if (!mc_mlir) { std::fprintf(stderr, "logosc: metacall MLIR gen failed\n"); return 1; }
            mc_stat_step(_mc_t, "mlir_gen", mi);
            mlir::PassManager mc_pm(&mc_ctx);
            mc_pm.addPass(logos::compat::create_scf_to_cf_pass());
            mc_pm.addPass(mlir::createConvertControlFlowToLLVMPass());
            mc_pm.addPass(mlir::createArithToLLVMConversionPass());
            mc_pm.addPass(mlir::createConvertFuncToLLVMPass());
            mc_pm.addPass(mlir::createReconcileUnrealizedCastsPass());
            if (mlir::failed(mc_pm.run(*mc_mlir))) {
                std::fprintf(stderr, "logosc: metacall MLIR lowering failed\n"); return 1;
            }
            mc_stat_step(_mc_t, "mlir->llvm", mi);
            mlir::registerBuiltinDialectTranslation(mc_ctx);
            mlir::registerLLVMDialectTranslation(mc_ctx);
            llvm::LLVMContext mc_llvm_ctx;
            auto mc_llvm = mlir::translateModuleToLLVMIR(*mc_mlir, mc_llvm_ctx);
            if (!mc_llvm) { std::fprintf(stderr, "logosc: metacall LLVM IR translate failed\n"); return 1; }
            mc_stat_step(_mc_t, "llvm_ir", mi);
            logos::compat::set_default_target_triple(*mc_llvm);
            llvm::InitializeNativeTarget();
            llvm::InitializeNativeTargetAsmPrinter();

            // Trim the meta-jit module to thunks + transitive deps. Without
            // this, ORC tries to materialize user main (and its stdlib
            // closure: fibers/TLS getters) which RuntimeDyld can't relocate.
            // Internalize everything except __metacall_thunk_*, then run
            // GlobalDCE to drop the now-dead user main and unrelated stdlib.
            llvm::internalizeModule(*mc_llvm, [](const llvm::GlobalValue& gv) {
                return gv.getName().starts_with("__metacall_thunk_");
            });
            {
                llvm::ModulePassManager mpm;
                mpm.addPass(llvm::GlobalDCEPass());
                llvm::ModuleAnalysisManager mam;
                llvm::PassBuilder pb;
                pb.registerModuleAnalyses(mam);
                mpm.run(*mc_llvm, mam);
            }

            auto mc_jit = build_jit_from_module(*mc_llvm, "logosc-metacall",
                                                /*with_process_symbols=*/true,
                                                &archive_paths);
            if (!mc_jit) return 1;
            mc_stat_step(_mc_t, "jit_build", mi);

            // The canonical metaprog extern set, shared with the
            // iteration-loop meta_jit (bind_metaprog_host_externs): thunk
            // modules can carry archived stdlib code referencing ANY of the
            // metaprog externs (whole-unit ORC materialization), so the two
            // JITs must expose an identical surface.
            if (!bind_metaprog_host_externs(*mc_jit, "mc_jit")) return 1;

            // Function-style macros (slice 1.3b): publish the per-site
            // arg-blob table so `logos_macro_arg(site, idx)` can resolve
            // bytes for each fn-macro thunk we are about to invoke.
            // Cleared right after the loop — the bytes live in the
            // snapshot we took before move-constructing mc_prog.
            g_macro_args = &saved_macro_args;
            struct MacroArgsGuard {
                ~MacroArgsGuard() { g_macro_args = nullptr; }
            } macro_args_guard;

            // Metaprog diagnostics channel for the metacall/token-macro thunk
            // path: `error()` from a #[token_macro] handler (wql!/trama!) or
            // any metacall callee pushes here; a non-empty set after the
            // invoke loop FAILS the compile — the same contract the
            // #[metaprog_handler] hook loop enforces. (Previously this path
            // left g_metaprog_diags null, silently swallowing `error()`.)
            std::vector<std::string> mc_diags;
            auto* prev_mc_diags = g_metaprog_diags;
            g_metaprog_diags = &mc_diags;

            // Step 3: invoke each thunk and splice the result into the AST.
            using RT = logos::compiler::lir::MetacallRetTag;
            bool any_spliced = false;
            for (const auto& site : saved_metacall_sites) {
                if (site.thunk_source().empty()) continue;
                if (site.ast_idx() >= asts.size()) continue;
                auto* sym = mc_jit->lookup(std::string(site.thunk_name()));
                if (!sym) {
                    std::fprintf(stderr, "logosc: metacall thunk lookup '%s': %s\n",
                                 std::string(site.thunk_name()).c_str(), mc_jit->error_str().c_str());
                    return 1;
                }

                // MC1.2: item-position metacall. Thunk is `unsafe fn() -> ()`
                // that internally calls `logos_emit_item_blob_subst(&blob)`,
                // which appends a fresh AST to g_asts. After invoke, mark the
                // METACALL_ITEM AST node consumed (CODE = METACALL_ITEM_DONE)
                // so the next sema pass skips it.
                if (site.ret_tag() == RT::ItemBlob) {
                    // Provenance for --dump-metaprog: file:line + callee at
                    // the original `metacall foo();` site. Read SRC_LINE
                    // from the METACALL_ITEM TOM before splice.
                    {
                        int line = 0;
                        if (site.ast_idx() < asts.size()) {
                            auto* h    = asts[site.ast_idx()].holder();
                            auto tom  = logos::writ::TinyMapView(logos::writ::arena_offset_t(site.expr_offset()), h);
                            auto av = tom.get(logos::compiler::ast::SRC_LINE.code);
                            if (!av.is_null() && av.is_value())
                                line = static_cast<int>(av.as_value<uint32_t>());
                        }
                        g_current_emit_ctx = EmitProvenance{
                            site.ast_idx() < filenames.size() ? filenames[site.ast_idx()] : std::string{},
                            line, std::string(site.callee_name()), std::string{}, std::string{}, mi,
                        };
                        g_current_emit_ctx_valid = true;
                    }
                    reinterpret_cast<void (*)()>(sym)();
                    g_current_emit_ctx_valid = false;
                    auto& doc = asts[site.ast_idx()];
                    auto* h   = doc.holder();
                    auto tom = logos::writ::TinyMapView(logos::writ::arena_offset_t(site.expr_offset()), h);
                    // Determine the DONE marker from the current CODE
                    // — metacall_item and fn_macro_call_item share the
                    // ItemBlob splice path but have distinct grammar
                    // node codes and matching DONE markers.
                    int32_t done_code = logos::compiler::ast::METACALL_ITEM_DONE.code;
                    {
                        auto cav = tom.get(logos::compiler::ast::CODE.code);
                        if (!cav.is_null() && cav.is_value()) {
                            int32_t cur = cav.as_value<int32_t>();
                            // MAPPING_DEF (ADR 0016) rides the same token-macro
                            // item seam — consumed sites get the same marker.
                            if (cur == logos::compiler::ast::FN_MACRO_CALL_ITEM.code
                                || cur == logos::compiler::ast::DEEM_DEF.code)
                                done_code = logos::compiler::ast::FN_MACRO_CALL_ITEM_DONE.code;
                            // A consumed mapping keeps its identity: a consumer
                            // compiling against the archive re-registers it for
                            // cross-module fusion (ADR 0016).
                            if (cur == logos::compiler::ast::MAPPING_DEF.code)
                                done_code = logos::compiler::ast::MAPPING_DEF_DONE.code;
                            // A consumed CONTAINER likewise keeps its identity
                            // (ADR 0020): archive consumers re-register the
                            // declaration for cross-module Canon reasoning.
                            if (cur == logos::compiler::ast::CONTAINER_DEF.code)
                                done_code = logos::compiler::ast::CONTAINER_DEF_DONE.code;
                        }
                    }
                    if (auto r = tom.put(
                            logos::compiler::ast::CODE.code,
                            logos::writ::AnyVal::from_value<int32_t>(done_code)
                            ); !r) {
                        std::fprintf(stderr,
                            "logosc: metacall item-splice: CODE put failed\n");
                        return 1;
                    }
                    any_spliced = true;
                    continue;
                }

                int64_t  i_val = 0;
                uint64_t u_val = 0;
                double   f_val = 0.0;
                bool     b_val = false;
                std::string s_val;
                std::string blob_bytes;  // for WritStatic ret
                bool is_float = false, is_bool = false, is_str = false, is_unsigned = false;
                bool is_writ_blob = false;
                switch (site.ret_tag()) {
                case RT::Bool:  b_val = reinterpret_cast<bool   (*)()>(sym)(); is_bool = true; break;
                case RT::I8:    i_val = reinterpret_cast<int8_t (*)()>(sym)(); break;
                case RT::I16:   i_val = reinterpret_cast<int16_t(*)()>(sym)(); break;
                case RT::I24:
                case RT::I32:   i_val = reinterpret_cast<int32_t(*)()>(sym)(); break;
                case RT::I56:
                case RT::I64:   i_val = reinterpret_cast<int64_t(*)()>(sym)(); break;
                case RT::U8:    u_val = reinterpret_cast<uint8_t (*)()>(sym)(); is_unsigned = true; break;
                case RT::U16:   u_val = reinterpret_cast<uint16_t(*)()>(sym)(); is_unsigned = true; break;
                case RT::U24:
                case RT::U32:   u_val = reinterpret_cast<uint32_t(*)()>(sym)(); is_unsigned = true; break;
                case RT::U56:
                case RT::U64:   u_val = reinterpret_cast<uint64_t(*)()>(sym)(); is_unsigned = true; break;
                case RT::F32:   f_val = reinterpret_cast<float (*)()>(sym)(); is_float = true; break;
                case RT::F64:   f_val = reinterpret_cast<double(*)()>(sym)(); is_float = true; break;
                case RT::Str: {
                    struct StrFat { const char* p; uint64_t n; };
                    auto sf = reinterpret_cast<StrFat(*)()>(sym)();
                    s_val.assign(sf.p ? sf.p : "", sf.n);
                    is_str = true; break;
                }
                case RT::WritStatic:
                case RT::ExprBlob: {
                    // WritStatic = { ptr: *const u8 }, DataPlain ≤ 16B, returned in rax.
                    // ExprBlob: identical ABI; nominal-only marker for AST-fragment payload.
                    // Layout in meta-jit rodata: [u64 size_le][bytes]; ptr points past the prefix.
                    auto blob_ptr = reinterpret_cast<const uint8_t* (*)()>(sym)();
                    if (!blob_ptr) {
                        std::fprintf(stderr, "logosc: metacall WritStatic/ExprBlob thunk returned null\n");
                        return 1;
                    }
                    uint64_t size = 0;
                    std::memcpy(&size, blob_ptr - 8, 8);
                    blob_bytes.assign(reinterpret_cast<const char*>(blob_ptr), size);
                    is_writ_blob = true;
                    break;
                }
                case RT::Writ: {
                    // Writ-returning thunk wraps callee in __metacall_freeze, which
                    // mallocs [u64 size][bytes] and returns ptr past prefix — same ABI
                    // as WritStatic from this side.
                    auto blob_ptr = reinterpret_cast<const uint8_t* (*)()>(sym)();
                    if (!blob_ptr) {
                        std::fprintf(stderr, "logosc: metacall Writ thunk returned null\n");
                        return 1;
                    }
                    uint64_t size = 0;
                    std::memcpy(&size, blob_ptr - 8, 8);
                    blob_bytes.assign(reinterpret_cast<const char*>(blob_ptr), size);
                    is_writ_blob = true;
                    break;
                }
                case RT::ItemBlob:
                    // Handled above via early-continue; unreachable here.
                    std::fprintf(stderr, "logosc: internal: ItemBlob fell through\n");
                    return 1;
                }

                std::string lit_text;
                int32_t new_code = logos::compiler::ast::LIT_INT;
                if (is_bool) { lit_text = b_val ? "true" : "false"; new_code = logos::compiler::ast::LIT_BOOL; }
                else if (is_writ_blob) { lit_text = blob_bytes; new_code = logos::compiler::ast::WRIT_BLOB; }
                else if (is_str) {
                    // LIT_STR AST VALUE is SOURCE form (parser convention:
                    // surrounding quotes, escapes undecoded) — render the
                    // JIT-returned VALUE back to source so the re-sema/codegen
                    // decode sees the same shape as parsed text. Same escape
                    // table as render_ctfe_lit / ctfe eval_lit_str.
                    lit_text = "\"";
                    for (char c : s_val) {
                        switch (c) {
                        case '\\': lit_text += "\\\\"; break;
                        case '"':  lit_text += "\\\""; break;
                        case '\n': lit_text += "\\n";  break;
                        case '\r': lit_text += "\\r";  break;
                        case '\t': lit_text += "\\t";  break;
                        case '\0': lit_text += "\\0";  break;
                        default:   lit_text += c;      break;
                        }
                    }
                    lit_text += "\"";
                    new_code = logos::compiler::ast::LIT_STR;
                }
                else if (is_float) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%.17g", f_val);
                    lit_text = buf;
                    if (lit_text.find('.') == std::string::npos
                        && lit_text.find('e') == std::string::npos
                        && lit_text.find('n') == std::string::npos
                        && lit_text.find('i') == std::string::npos)
                        lit_text += ".0";
                    new_code = logos::compiler::ast::LIT_FLOAT;
                }
                else if (is_unsigned) { lit_text = std::to_string(u_val); }
                else { lit_text = std::to_string(i_val); }

                auto& doc = asts[site.ast_idx()];
                auto* h = doc.holder();
                auto tom = logos::writ::TinyMapView(logos::writ::arena_offset_t(site.expr_offset()), h);

                logos::writ::AnyVal value_av;
                if (is_bool) {
                    value_av = logos::writ::AnyVal::from_value<uint8_t>(b_val ? 1 : 0);
                } else {
                    auto str_exp = doc.make_string(lit_text);
                    if (!str_exp) {
                        std::fprintf(stderr, "logosc: metacall splice: make_string OOM\n");
                        return 1;
                    }
                    value_av = str_exp->to_anyval();
                    tom = logos::writ::TinyMapView(logos::writ::arena_offset_t(site.expr_offset()), h);
                }

                if (auto r = tom.put(logos::compiler::ast::CODE.code,
                                      logos::writ::AnyVal::from_value<int32_t>(new_code)
                                      ); !r) {
                    std::fprintf(stderr, "logosc: metacall splice: CODE put failed\n");
                    return 1;
                }
                tom = logos::writ::TinyMapView(logos::writ::arena_offset_t(site.expr_offset()), h);
                tom.set_schema_type_code(
                    logos::writ::schema::ast(static_cast<int32_t>(new_code)));
                if (auto r = tom.put(logos::compiler::ast::VALUE.code, value_av); !r) {
                    std::fprintf(stderr, "logosc: metacall splice: VALUE put failed\n");
                    return 1;
                }
                any_spliced = true;
            }

            // Drain the thunk diag channel: any `error()` reported during the
            // invoke loop fails the compile with the reported messages.
            g_metaprog_diags = prev_mc_diags;
            if (!mc_diags.empty()) {
                for (const auto& d : mc_diags)
                    std::fprintf(stderr, "error: %s\n", d.c_str());
                return 1;
            }

            // Step 4: re-run sema. Sites should disappear (METACALL→LIT_INT).
            // Loop continues only if new sites somehow appeared.
            (void)any_spliced;
            auto _mc_t_resema = std::chrono::steady_clock::now();
            prog = logos::compiler::sema_lower(asts, filenames, from_binary, default_opts, is_lazy, module_ids);
            mc_stat_step(_mc_t_resema, "resema_after_splice", mi);
            prog.print_diags(stderr);
            if (!prog.ok()) return 1;
        }
    }
    report("sema+lower (final)");

    // ── --test post-sema runner synthesis ──────────────────────────────
    // Augment the ast-level test list with `#[test]` fns that surfaced
    // only after metacall expansion (item-position macros like
    // `int_module!{...}` emit their test fns during sema). Then build
    // the runner source from the unified list, parse it, append, and
    // re-run sema one final time so the runner's `main()` is part of
    // the program before mono/mlir-gen.
    if (test_mode) {
        // Dedup pre-sema discovered tests by name so they can be
        // matched against the post-sema scan.
        std::set<std::string> seen_names;
        for (const auto& t : tests) seen_names.insert(t.name);
        for (const auto& fp : prog.functions) {
            if (!fp) continue;
            if (!fp.is_test()) continue;
            // Filter to user-package fns only — stdlib modules also
            // contain `#[test]` fns (placeholders, internal smoke
            // tests) that would otherwise leak into the runner.
            if (fp.from_binary_module()) continue;
            if (!entry_pkg.empty() && fp.package() != entry_pkg) continue;
            // The runner calls test fns by their source name. fp->name
            // carries the post-collect mangled form (`pkg$base__f__sig`);
            // use the shared bare_fn_name helper to recover the base.
            std::string nm(logos::compiler::bare_fn_name(fp.name()));
            if (seen_names.insert(nm).second) {
                tests.push_back({nm, fp.should_panic(), fp.ignored(),
                                 std::string(fp.should_panic_expected_msg())});
            }
        }
        if (tests.empty()) {
            std::fprintf(stderr, "logosc: --test: no `#[test]` functions found\n");
            return 1;
        }
        // Synthesise the runner source. Same shape as the pre-sema
        // synth used to be — package matches entry_pkg so bare-name
        // calls resolve to the user-visible test fns.
        std::string s;
        s  = "package " + entry_pkg + ";\n";
        s += "extern fn write(fd: i32, buf: *const u8, n: i64) -> i64;\n";
        s += "extern fn logos_run_with_recovery(fn_ptr: fn()) -> i32;\n";
        s += "extern fn logos_test_print_start(name: *const u8, n: i64);\n";
        s += "extern fn logos_test_print_ok();\n";
        s += "extern fn logos_test_print_failed();\n";
        s += "extern fn logos_test_print_failed_no_panic();\n";
        s += "extern fn logos_test_print_failed_expected_mismatch(p: *const u8, n: i64);\n";
        s += "extern fn logos_panic_msg_contains(p: *const u8, n: i64) -> i32;\n";
        s += "extern fn logos_test_print_ignored();\n";
        s += "extern fn logos_test_print_summary(p: i32, f: i32, i: i32) -> i32;\n";
        s += "pub unsafe fn main() -> i32 {\n";
        s += "    let mut passed: i32 = 0 as i32;\n";
        s += "    let mut failed: i32 = 0 as i32;\n";
        s += "    let mut ignored: i32 = 0 as i32;\n";
        for (const auto& t : tests) {
            std::string nm_lit = "\"" + t.name + "\"";
            s += "    {\n";
            s += "        let nm: str = " + nm_lit + ";\n";
            s += "        logos_test_print_start(nm.as_ptr(), nm.len());\n";
            if (t.ignored) {
                s += "        logos_test_print_ignored();\n";
                s += "        ignored = ignored + (1 as i32);\n";
            } else {
                s += "        let rv: i32 = logos_run_with_recovery(" + t.name + ");\n";
                if (t.should_panic) {
                    if (!t.expected_msg.empty()) {
                        std::string esc;
                        esc.reserve(t.expected_msg.size() + 2);
                        for (char c : t.expected_msg) {
                            if (c == '\\' || c == '"') esc.push_back('\\');
                            esc.push_back(c);
                        }
                        std::string lit = "\"" + esc + "\"";
                        s += "        if rv != (0 as i32) {\n";
                        s += "            let exp: str = " + lit + ";\n";
                        s += "            let ok: i32 = logos_panic_msg_contains(exp.as_ptr(), exp.len());\n";
                        s += "            if ok != (0 as i32) {\n";
                        s += "                logos_test_print_ok();\n";
                        s += "                passed = passed + (1 as i32);\n";
                        s += "            } else {\n";
                        s += "                logos_test_print_failed_expected_mismatch(exp.as_ptr(), exp.len());\n";
                        s += "                failed = failed + (1 as i32);\n";
                        s += "            }\n";
                        s += "        } else {\n";
                        s += "            logos_test_print_failed_no_panic();\n";
                        s += "            failed = failed + (1 as i32);\n";
                        s += "        }\n";
                    } else {
                        s += "        if rv != (0 as i32) {\n";
                        s += "            logos_test_print_ok();\n";
                        s += "            passed = passed + (1 as i32);\n";
                        s += "        } else {\n";
                        s += "            logos_test_print_failed_no_panic();\n";
                        s += "            failed = failed + (1 as i32);\n";
                        s += "        }\n";
                    }
                } else {
                    s += "        if rv == (0 as i32) {\n";
                    s += "            logos_test_print_ok();\n";
                    s += "            passed = passed + (1 as i32);\n";
                    s += "        } else {\n";
                    s += "            logos_test_print_failed();\n";
                    s += "            failed = failed + (1 as i32);\n";
                    s += "        }\n";
                }
            }
            s += "    }\n";
        }
        s += "    return logos_test_print_summary(passed, failed, ignored);\n";
        s += "}\n";

        logos::compiler::LogosParser parser(s);
        auto runner_ast = parser.parse_module();
        if (runner_ast.is_null()) {
            std::fprintf(stderr,
                "logosc: --test: failed to parse synthesised main:\n%s\n", s.c_str());
            return 1;
        }
        filenames.push_back("<test_main_synth>");
        from_binary.push_back(false);
        asts.push_back(std::move(runner_ast));
        if (trace) {
            std::fprintf(stderr,
                "[trace] --test: synthesised main calling %zu test fn(s)\n",
                tests.size());
        }
        // Re-run sema with the runner included. The metacall splice loop
        // has already converged so this should be a single straight pass
        // (no new metacall_sites). g_user_root_idx points to the runner
        // file by convention (asts.back()).
        //
        // Use fresh SemaOptions (no shared cache) — the cache's persisted
        // user-fn tables would re-flag every already-collected fn as
        // duplicate when this sema pass walks the asts vector again.
        g_user_root_idx = asts.size() - 1;
        logos::compiler::SemaOptions runner_opts;
        runner_opts.cfg_flags = cfg_flags;
        prog = logos::compiler::sema_lower(asts, filenames, from_binary, runner_opts, is_lazy, module_ids);
        prog.print_diags(stderr);
        if (!prog.ok()) return 1;
        if (!prog.metacall_sites.empty()) {
            std::fprintf(stderr,
                "logosc: --test: synthesised runner introduced new metacall sites; "
                "this isn't supported. Avoid macros that emit metacall in the test "
                "harness path.\n");
            return 1;
        }
        report("sema+lower (test runner)");
    }

    // ── --dump-metaprog: write metafn-emitted ASTs as Logos source ──────────
    // Iterates ast_provenance (parallel to asts, sparse — only emit-tracked
    // docs have entries). Per emitted doc, builds a directory whose name
    // identifies the producing metacall by callee + file:line, and writes
    // `post_quote.logos` with the full module rendered through Stage 2 of
    // the AST pretty-printer + `post_mono_index.txt` listing fn names that
    // grep into the global post-mono MLIR / post-mlirgen LLVM IR snapshots
    // (written further down, after mlir_gen and translateModuleToLLVMIR).
    //
    // Per-metacall MLIR slicing isn't done yet — would need ast_idx threaded
    // through LFunction + mono_clone, which mono drops today. Pragmatic v1.5
    // is grep-driven: navigate from post_quote → fn names → global IR files.
    std::vector<std::pair<size_t, std::string>> per_metacall_dirs;  // (ast_idx, dir)
    if (!dump_metaprog_dir.empty()) {
        // mkdir -p the root.
        auto mkdir_p = [](const std::string& path) {
            for (size_t i = 1; i <= path.size(); ++i) {
                if (i == path.size() || path[i] == '/') {
                    std::string seg = path.substr(0, i);
                    if (seg.empty()) continue;
                    ::mkdir(seg.c_str(), 0755);  // EEXIST tolerated
                }
            }
        };
        auto sanitize = [](std::string s) {
            for (auto& c : s) {
                if (c == '/' || c == '\\' || c == ':' || c == ' ' || c == '<' || c == '>' || c == '"' || c == '\'')
                    c = '_';
            }
            return s;
        };
        auto basename_no_ext = [](std::string_view p) -> std::string {
            auto slash = p.find_last_of('/');
            std::string_view bn = (slash == std::string::npos) ? p : p.substr(slash + 1);
            auto dot = bn.find_last_of('.');
            if (dot != std::string_view::npos) bn = bn.substr(0, dot);
            return std::string(bn);
        };
        auto write_file = [](const std::string& path, const std::string& content) {
            FILE* f = std::fopen(path.c_str(), "w");
            if (!f) {
                std::fprintf(stderr, "logosc: --dump-metaprog: cannot write %s\n", path.c_str());
                return;
            }
            std::fwrite(content.data(), 1, content.size(), f);
            std::fclose(f);
        };

        mkdir_p(dump_metaprog_dir);

        // Parse comma-separated filter patterns. Empty / "*" = match all.
        // Per pattern, match if it equals "*" OR appears as a substring of
        // ctx.callee_name / ctx.trigger / "<basename>:<line>" /
        // "<basename>" — covers the common selector forms (callee name,
        // trigger annotation, file:line, file). OR'd across all patterns.
        std::vector<std::string> filter_pats;
        {
            std::string_view f = dump_metaprog_filter;
            while (!f.empty()) {
                auto comma = f.find(',');
                std::string_view part = (comma == std::string_view::npos) ? f : f.substr(0, comma);
                while (!part.empty() && part.front() == ' ') part.remove_prefix(1);
                while (!part.empty() && part.back()  == ' ') part.remove_suffix(1);
                if (!part.empty()) filter_pats.emplace_back(part);
                if (comma == std::string_view::npos) break;
                f.remove_prefix(comma + 1);
            }
        }
        auto matches_filter = [&](const EmitProvenance& ctx) {
            if (filter_pats.empty()) return true;  // no filter = all
            std::string base_noext = std::string(basename_no_ext(ctx.src_file));
            // Basename WITH extension — for `<file.logos>:<line>` selectors.
            std::string base_full;
            {
                auto slash = ctx.src_file.find_last_of('/');
                base_full = (slash == std::string::npos) ? ctx.src_file : ctx.src_file.substr(slash + 1);
            }
            std::string line_str = std::to_string(ctx.src_line);
            std::string fl_noext = base_noext + ":" + line_str;
            std::string fl_full  = base_full  + ":" + line_str;
            std::string fl_path  = ctx.src_file + ":" + line_str;
            for (const auto& pat : filter_pats) {
                if (pat == "*") return true;
                if (ctx.callee_name.find(pat) != std::string::npos) return true;
                if (!ctx.trigger.empty() && ctx.trigger.find(pat) != std::string::npos) return true;
                if (!ctx.target_name.empty() && ctx.target_name.find(pat) != std::string::npos) return true;
                if (fl_noext.find(pat) != std::string::npos) return true;
                if (fl_full.find(pat)  != std::string::npos) return true;
                if (fl_path.find(pat)  != std::string::npos) return true;
                if (ctx.src_file.find(pat) != std::string::npos) return true;
            }
            return false;
        };

        // Render each emit-tracked doc. The provenance vector parallels
        // g_asts; entries left as nullopt are docs the user wrote, not
        // metafn output.
        for (size_t i = 0; i < ast_provenance.size(); ++i) {
            if (!ast_provenance[i]) continue;
            if (i >= asts.size()) continue;
            const auto& ctx = *ast_provenance[i];
            if (!matches_filter(ctx)) continue;
            std::string base_file = basename_no_ext(ctx.src_file);
            if (base_file.empty()) base_file = "unknown";
            std::string label = ctx.callee_name.empty()
                ? (ctx.trigger.empty() ? std::string{"emit"} : ctx.trigger)
                : ctx.callee_name;
            std::string slot = sanitize(label) + "__" + sanitize(base_file)
                + "_" + std::to_string(ctx.src_line);
            // Disambiguate when the same trigger fires N times on the same line.
            std::string dir = dump_metaprog_dir + "/" + slot;
            int suffix = 0;
            std::string trial = dir;
            while (::mkdir(trial.c_str(), 0755) != 0) {
                if (errno != EEXIST) break;
                trial = dir + "_" + std::to_string(++suffix);
            }
            dir = trial;
            mkdir_p(dir);

            auto* h = asts[i].holder();
            auto root_off = logos::writ::WritAccess::root_offset(asts[i]);
            std::string body =
                logos::compiler::render_module_source_for_dump(h, root_off);
            auto fn_names =
                logos::compiler::collect_fn_names_for_dump(h, root_off);

            std::string header;
            header += "// Auto-generated by logosc --dump-metaprog\n";
            header += "// metacall callee: " + ctx.callee_name + "\n";
            if (!ctx.trigger.empty()) header += "// trigger:          #[" + ctx.trigger + "]\n";
            if (!ctx.target_name.empty()) header += "// target:           " + ctx.target_name + "\n";
            header += "// site:             " + ctx.src_file + ":" + std::to_string(ctx.src_line) + "\n";
            header += "// metaprog iter:    " + std::to_string(ctx.iter_seq) + "\n";
            header += "// ast index:        " + std::to_string(i) + "\n";
            if (!fn_names.empty()) {
                header += "// post-mono / post-mlirgen: see ../_global_post_mono.mlir\n";
                header += "//                          and ../_global_post_mlirgen.ll;\n";
                header += "//                          grep names in ./post_mono_index.txt\n";
            }
            header += "\n";
            write_file(dir + "/post_quote.logos", header + body);

            // Per-metacall index: navigable fn names to grep in global IR
            // snapshots. Names are pre-mangling — sema may prefix `pkg$` or
            // `pkg.Type__`, mono may suffix `__T1__T2`; substring-match is
            // robust to both. Empty for emit docs that produced no fns
            // (rare — usually annotations or const_defs).
            if (!fn_names.empty()) {
                std::string idx;
                idx += "# Bare fn names defined by this metacall's emitted AST.\n";
                idx += "# Sema/mono may prefix pkg/type qualifiers and suffix mono\n";
                idx += "# args; substring-grep `<name>` in:\n";
                idx += "#   - ../_global_post_mono.mlir   (post-mono MLIR module)\n";
                idx += "#   - ../_global_post_mlirgen.ll  (post-mlirgen LLVM IR)\n";
                idx += "#\n";
                for (const auto& n : fn_names) idx += n + "\n";
                write_file(dir + "/post_mono_index.txt", idx);
            }

            per_metacall_dirs.emplace_back(i, dir);
        }
    }

    // ── ADR 0021 §3 (Phase 2b): mono → metaclass-factory demand drain ────
    // The terminal tail below (hook strip → reflection → pre-mono borrow →
    // mono) runs in a bounded loop. When the terminal mono surfaces factory
    // demands (`CtrClass<CFG>` instantiations recorded by CFG content
    // hash), the driver renders one
    //   `metacall __container_factory("<016x hash>", "<CFG doc source>");`
    // chunk per demand into the fresh `logos.gen` package, feeds it through
    // logos_emit_source, re-enters the metaprog dispatch (which JIT-invokes
    // the factory through the ordinary metacall-item thunk and splices/emits
    // its items), re-runs the terminal sema, and loops so the tail re-checks
    // the enlarged program.
    //
    // Driver-side dedup is load-bearing: mono's factory_demand_hashes_ is
    // per-Mono-instance, so each fresh mono pass re-fires demands for configs
    // whose satisfaction is not yet consulted. Re-fires of a drained hash are
    // STRUCTURAL and expected — the CtrClass<CFG> marker struct itself
    // re-instantiates every round (the demand hook fires on marker
    // instantiation, not on an unresolved impl). The satisfaction signal
    // (Phase 4a) is therefore a program-content probe, not the re-fire: after
    // a drained round the program must actually CONTAIN the generated family
    // (any `Hs<hex>`-named function — handle statics, node methods, the
    // CtrFamily impl fns all carry the hash-derived family name). Absence
    // after a drain round = the factory ran but produced nothing for that
    // config → hard error instead of a silent create_ctr resolution failure.
    std::unordered_set<uint64_t>& drained_factory_hashes = g_early_drained_hashes;
    constexpr int kMaxFactoryDrainRounds = 3;
    for (int drain_round = 0; ; ++drain_round) {

    // Factory availability, probed on the post-sema (pre-mono/prune) program:
    // an emitted `use` cannot LOAD a module, so the drain chunk's
    // `use logos.lcm.canon.container_item;` resolves only when the program
    // already pulled the handler module in (any unit declaring containers
    // does; plain CtrClass users must import it explicitly). Mirrors
    // lower_container_def's func_overloads_ gate, at driver level: the
    // factory's bare name must be present among the program's functions.
    bool factory_available = false;
    for (const auto& f : prog.functions) {
        if (f && logos::compiler::bare_fn_name(f.name()) == "__container_factory") {
            factory_available = true;
            break;
        }
    }

    // Phase 7 slice 19: strip metaprog hook fns from the FINAL prog. Their
    // bodies reference host-only symbols (logos_emit_source, etc.) that the
    // user's compiled artifact has no business carrying — they're purely
    // compile-time machinery. Hooks are still validated (sema lowered them)
    // but won't reach mono / MLIR / linker.
    {
        std::set<std::string> hook_names;
        for (const auto& mh : prog.metaprog_handlers) hook_names.insert(std::string(mh.hook_fn()));
        prog.functions.erase(
            std::remove_if(prog.functions.begin(), prog.functions.end(),
                [&](const auto& f) {
                    return hook_names.count(std::string(logos::compiler::bare_fn_name(f.name()))) > 0;
                }),
            prog.functions.end());
    }

    for (auto& __s : binary_symbols) logos::compiler::lir_mirror_map_put_null(prog, prog.binary_symbols, __s);

    // ── Step 2b+: Reflection TypeInfo emission (pre-mono, concrete types only)
    prog = logos::compiler::reflection_emit(std::move(prog));
    report("reflection_emit");

    // ── Step 2b++: PRE-mono borrow check (P2-10) ─────────────────
    // Monomorphization drops generic templates (replacing them with concrete
    // specializations), so a generic fn that is NEVER instantiated would never
    // be borrow-checked. Run the borrow checker on the pre-mono program too so
    // generic bodies are checked directly (Rust parity); move tracking is
    // imprecise on TypeVars so this runs exclusivity-only — concrete moves are
    // checked on the monomorphized specializations by the post-mono pass.
    prog = logos::compiler::borrow_check(std::move(prog), /*generic_templates_only=*/true);
    prog.print_diags(stderr);
    if (!prog.ok()) return 1;

    // ── Step 2c: Monomorphization (also emits L-IR Writ mirror) ─
    {
        logos::compiler::MonoOpts mopts;
        mopts.stdlib_exports = &stdlib_exports;
        prog = logos::compiler::mono_pass(std::move(prog), std::move(mopts));
    }
    prog.print_diags(stderr);
    if (!prog.ok()) return 1;
    report("mono");

    // ── Drain decision ───────────────────────────────────────────
    if (prog.factory_demands.empty()) break;

    // Deep-copy the fresh (not-yet-drained) demands: `prog` is replaced by
    // the re-sema below while the chunk loop still reads them.
    std::vector<logos::compiler::lir::LProgram::FactoryDemand> fresh_demands;
    for (const auto& fd : prog.factory_demands)
        if (!drained_factory_hashes.count(fd.cfg_hash)) fresh_demands.push_back(fd);

    // ── Satisfaction probe (Phase 4a) ────────────────────────────────────
    // Every re-fired already-drained demand must be backed by its generated
    // family in THIS program. Probe: any function whose (bare or mangled)
    // name contains the hash-derived family name `Hs<016x>` — create/open/
    // insert/get_or/size and the node methods all carry it. Cheap: runs only
    // for hashes that survived a drain round, and the 16-hex-digit needle
    // cannot false-positive.
    for (const auto& fd : prog.factory_demands) {
        if (!drained_factory_hashes.count(fd.cfg_hash)) continue;
        char hex[17];
        std::snprintf(hex, sizeof hex, "%016llx", (unsigned long long)fd.cfg_hash);
        std::string fam = std::string("Hs") + hex;
        bool present = false;
        for (const auto& f : prog.functions) {
            if (f && std::string_view(f.name()).find(fam) != std::string_view::npos) {
                present = true;
                break;
            }
        }
        if (!present) {
            std::fprintf(stderr,
                "logosc: metaclass: factory failed to produce the family for "
                "@hs_%s (demand %s<@hs_%s>, %s — drained, but no %s* function "
                "exists after the drain round)\n",
                hex, fd.base.c_str(), hex, fd.cname.c_str(), fam.c_str());
            return 1;
        }
    }

    if (fresh_demands.empty()) {
        // Steady state: every demand drained + family present. But a demand
        // sema (re-)marked REQUIRED in THIS round means the post-drain
        // re-sema still had to defer a diagnostic against the marker — the
        // factory produced the family yet not the impl the use site needs.
        for (const auto& fd : prog.factory_demands) {
            if (!fd.required) continue;
            std::fprintf(stderr,
                "logosc: metaclass: factory did not satisfy the deferred "
                "trait obligation for %s<@hs_%016llx> (%s) — the CtrFamily "
                "impl is missing after the drain round\n",
                fd.base.c_str(), (unsigned long long)fd.cfg_hash, fd.cname.c_str());
            return 1;
        }
        break;
    }

    if (!factory_available) {
        // A REQUIRED demand (sema deferred a create_ctr-style diagnostic
        // against it) cannot proceed without the factory — hard error, with
        // the fix spelled out. Purely structural demands stay a note: the
        // CtrClass marker template instantiates fine on its own
        // (metaclass identity is meaningful without the family).
        for (const auto& fd : fresh_demands) {
            if (!fd.required) continue;
            std::fprintf(stderr,
                "logosc: metaclass: %s<@hs_%016llx> (%s) requires the "
                "container factory, but logos.lcm.canon.container_item is "
                "not loaded — add `use logos.lcm.canon.container_item;` to "
                "the declaring unit (ADR 0021 Phase 4a)\n",
                fd.base.c_str(), (unsigned long long)fd.cfg_hash, fd.cname.c_str());
            return 1;
        }
        std::fprintf(stderr,
            "metaclass: %zu factory demand(s) pending but "
            "logos.lcm.canon.container_item is not loaded — drain skipped "
            "(import the handler module to run the factory; ADR 0021 Phase 2b)\n",
            fresh_demands.size());
        break;
    }
    if (drain_round + 1 >= kMaxFactoryDrainRounds) {
        std::fprintf(stderr,
            "logosc: metaclass factory drain did not converge in %d rounds "
            "(%zu demand(s) still fresh)\n",
            kMaxFactoryDrainRounds, fresh_demands.size());
        return 1;
    }

    // Render + emit one metacall chunk per fresh demand. The emit globals
    // (g_asts/g_filenames/…) are still wired from the splice phase above;
    // g_any_emitted pointed at a splice-loop local that is gone — rewire.
    bool drain_any_emitted = false;
    g_any_emitted = &drain_any_emitted;
    bool emitted_chunk = false;
    for (const auto& fd : fresh_demands) {
        auto src_it = prog.wstatic_sources.find(fd.cfg_hash);
        if (src_it == prog.wstatic_sources.end()) {
            std::fprintf(stderr,
                "logosc: metaclass: demand %s<@hs_%016llx> (%s) has no captured "
                "CFG document source (sema wstatic_sources gap)\n",
                fd.base.c_str(), (unsigned long long)fd.cfg_hash, fd.cname.c_str());
            return 1;
        }
        char hex[17];
        std::snprintf(hex, sizeof hex, "%016llx", (unsigned long long)fd.cfg_hash);
        // Escape the CFG source for a Logos string literal (same table as
        // render_ctfe_lit's str case).
        std::string esc = esc_lit(src_it->second);
        std::string chunk;
        chunk += "package logos.gen;\n";
        chunk += "use logos.lcm.canon.container_item;\n";
        // The factory's family blobs splice INTO THIS CHUNK's unit, and a
        // generated `impl BtLeaf/BtBranch/Ctr` is trait-checked at COLLECT
        // time against the DECLARING unit's imports (a blob-emitted `use`
        // resolves names too late for that check) — the declaring unit of a
        // drained family is this chunk itself, so it must import the trait
        // hub. The module is loaded transitively (container_item → bt.ops →
        // bt.map) whenever the factory is available, and this `use` rides
        // the chunk source from parse time.
        chunk += "use logos.mem.bt.map;\n";
        chunk += "metacall __container_factory(\"";
        chunk += hex;
        chunk += "\", \"";
        chunk += esc;
        chunk += "\");\n";
        if (logos_emit_source(chunk.c_str())) emitted_chunk = true;
        drained_factory_hashes.insert(fd.cfg_hash);
        std::fprintf(stderr, "metaclass: factory drained @hs_%s\n", hex);
    }
    if (!emitted_chunk) break;  // defensive: emit-seen dedup swallowed everything

    // Re-enter the metaprog dispatch: it discovers the chunks' METACALL_ITEM
    // sites, JIT-compiles + invokes the factory thunks (splicing/emitting the
    // factory's items) and marks the sites DONE. Options mirror the Step 2b
    // discovery-loop wiring.
    {
        auto dopts = dispatch_opts();
        if (logos::compiler::run_metaprog_dispatch(
                asts, filenames, from_binary, pre_dispatch_entry_idx, dopts) != 0)
            return 1;
    }

    // Terminal re-sema so the factory-emitted items join the program; the
    // loop then re-runs the tail (hook strip, reflection, borrow, mono) on
    // the enlarged program. --test mode re-semas with cache-less options for
    // the same reason the runner synthesis does (the shared cache's persisted
    // user-fn tables re-flag re-walked fns as duplicates).
    {
        logos::compiler::SemaOptions drain_opts = default_opts;
        if (test_mode) {
            drain_opts = logos::compiler::SemaOptions{};
            drain_opts.cfg_flags = cfg_flags;
        }
        prog = logos::compiler::sema_lower(asts, filenames, from_binary,
                                           drain_opts, is_lazy, module_ids);
    }
    prog.print_diags(stderr);
    if (!prog.ok()) return 1;
    if (!prog.metacall_sites.empty()) {
        std::fprintf(stderr,
            "logosc: metaclass factory drain surfaced new metacall sites; "
            "unsupported inside the drain loop (ADR 0021 Phase 2b)\n");
        return 1;
    }

    }  // factory drain loop

    // ── the fixpoint stopped moving: what did it never get? ──────
    // Every producer has now had its last chance — the metaprog rounds, the
    // plan instantiations, and the post-mono factory drain are all behind us.
    // A demand still standing here was never satisfied, and saying so is the
    // whole reason a deferral records WHO it waits for: without this the same
    // situation surfaces as a cascade somewhere downstream (an "undefined
    // function" at a call site, an "unknown type" inside generated code) with
    // nothing pointing back at the thing that never got produced.
    if (prog.has_pending()) {
        for (const auto& p : prog.pending)
            std::fprintf(stderr,
                "%s:%d: error: %s is still waiting for %s — %s never produced "
                "it\n",
                p.file.c_str(), p.line,
                p.where.empty() ? "this program" : p.where.c_str(),
                p.what.c_str(), p.who.c_str());
        return 1;
    }

    // ── Step 2d: Borrow checking ─────────────────────────────────
    prog = logos::compiler::borrow_check(std::move(prog));
    prog.print_diags(stderr);
    if (!prog.ok()) return 1;
    report("borrow");

    // MC1.2: drop synthesised metacall thunks before final codegen — their
    // bodies reference JIT-only host shims (logos_emit_item_blob_subst,
    // logos_quote_expr_subst, …) which aren't defined in the user binary.
    prog.functions.erase(
        std::remove_if(prog.functions.begin(), prog.functions.end(),
            [](const auto& f) {
                return f && f.name().rfind("__metacall_thunk_", 0) == 0;
            }),
        prog.functions.end());

    // ── Steps 3-6: shared MLIR/LLVM lowering tail (compile_pipeline.cpp).
    // Handles mlir_gen → MLIR→LLVM lowering → opt pipeline → object emission.
    // Honors the --emit-mlir / --emit-llvm short-circuits internally.
    {
        logos::compiler::LowerEmitOpts lopts;
        lopts.opt_level         = opt_level;
        lopts.function_sections = true;
        lopts.emit_mlir         = emit_mlir;
        lopts.emit_llvm         = emit_llvm;
        lopts.emit_llvm_opt     = emit_llvm_opt;
        lopts.overflow_checks   = overflow_checks;
        lopts.target_cpu        = target_cpu;
        lopts.debug_info        = debug_g;
        lopts.source_path       = input_path ? input_path : "";
        lopts.dump_metaprog_dir = dump_metaprog_dir;
        int rc = logos::compiler::lower_and_emit_object(prog, output_path, lopts);
        if (rc != 0) return rc;
        report("codegen+write");
        if (emit_mlir || emit_llvm || emit_llvm_opt) return 0;
    }

    std::fprintf(stderr, "logosc: wrote %s\n", output_path);

    // LOGOS_TEXT_SIZE=1 — invoke `nm --print-size --size-sort --radix=d` on
    // the freshly-written object, dump top symbols by .text size to stderr.
    // Counterpart to LOGOS_MONO_STATS for measuring how monomorphization
    // decisions translate into actual emitted code size.
    if (const char* e = std::getenv("LOGOS_TEXT_SIZE"); e && e[0] != '0' && e[0] != '\0') {
        size_t top = 20;
        if (const char* n = std::getenv("LOGOS_TEXT_SIZE_TOP"))
            if (size_t v = std::strtoul(n, nullptr, 10); v > 0) top = v;
        // `nm --print-size --size-sort --radix=d <file>` lists symbols with
        // non-zero size, ascending. Pipe through `tail -N` for top-N.
        std::string cmd = "nm --print-size --size-sort --radix=d ";
        cmd += output_path;
        cmd += " 2>/dev/null | awk '$3 ~ /^[Tt]$/ {print}' | tail -";
        cmd += std::to_string(top);
        std::fprintf(stderr, "[text-size] top %zu .text symbols in %s by size:\n",
                     top, output_path);
        std::fflush(stderr);
        std::system(cmd.c_str());
        // Also dump total .text size via `size`.
        std::string sz = "size --format=sysv ";
        sz += output_path;
        sz += " 2>/dev/null | awk '$1==\".text\" {printf \"[text-size] .text total=%s bytes\\n\", $2}'";
        std::system(sz.c_str());
    }

    // --stats: print a per-phase breakdown of compile time. Top-level phases
    // are wall-clock-disjoint (sum to wall-clock); metaprog/metacall iters'
    // sub-phases are NESTED inside the top-level "sema+lower (final)" sample,
    // so we report them separately and label them as a subset.
    if (stats_flag) {
        auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t_compile_start).count();
        // Three buckets: top-level (iter<0), metaprog iters (iter>=0, label
        // unprefixed), metacall iters (iter>=0, label starts with "mc_").
        std::map<std::string, int64_t> top_by_label;
        std::map<std::string, int>     top_count;
        std::map<std::string, int64_t> mp_by_label;
        std::map<std::string, int>     mp_count;
        std::map<std::string, int64_t> mc_by_label;
        std::map<std::string, int>     mc_count;
        int64_t mp_total = 0, mc_total = 0;
        int     mp_iters = 0,  mc_iters = 0;
        for (auto& s : top_stats.samples) {
            if (s.iter < 0) {
                top_by_label[s.label] += s.ms;
                top_count[s.label]    += 1;
            } else if (s.label.rfind("mc_", 0) == 0) {
                std::string short_label = s.label.substr(3);
                mc_by_label[short_label] += s.ms;
                mc_count[short_label]    += 1;
                mc_total                 += s.ms;
                if (s.iter + 1 > mc_iters) mc_iters = s.iter + 1;
            } else {
                mp_by_label[s.label] += s.ms;
                mp_count[s.label]    += 1;
                mp_total             += s.ms;
                if (s.iter + 1 > mp_iters) mp_iters = s.iter + 1;
            }
        }
        auto rule = "─────────────────────────────────────────────";
        std::fprintf(stderr, "%s\n", rule);
        std::fprintf(stderr, "  --stats : phase timings (ms)\n");
        std::fprintf(stderr, "%s\n", rule);
        std::fprintf(stderr, "  Top-level pipeline   (sums to wall-clock)\n");
        for (auto& [label, ms] : top_by_label) {
            std::fprintf(stderr, "    %-26s %8lld  (×%d)\n",
                         label.c_str(), (long long)ms, top_count[label]);
        }
        if (!mp_by_label.empty()) {
            std::fprintf(stderr, "%s\n", rule);
            std::fprintf(stderr, "  Metaprog dispatch iters  (NESTED in sema+lower)\n");
            std::fprintf(stderr, "  iterations: %d\n", mp_iters);
            for (auto& [label, ms] : mp_by_label) {
                std::fprintf(stderr, "    %-26s %8lld  (×%d)\n",
                             label.c_str(), (long long)ms, mp_count[label]);
            }
            std::fprintf(stderr, "    %-26s %8lld\n",
                         "subtotal", (long long)mp_total);
        }
        if (!mc_by_label.empty()) {
            std::fprintf(stderr, "%s\n", rule);
            std::fprintf(stderr, "  Metacall thunk iters     (NESTED in sema+lower)\n");
            std::fprintf(stderr, "  iterations: %d\n", mc_iters);
            for (auto& [label, ms] : mc_by_label) {
                std::fprintf(stderr, "    %-26s %8lld  (×%d)\n",
                             label.c_str(), (long long)ms, mc_count[label]);
            }
            std::fprintf(stderr, "    %-26s %8lld\n",
                         "subtotal", (long long)mc_total);
        }
        std::fprintf(stderr, "%s\n", rule);
        std::fprintf(stderr, "  %-28s %8lld\n", "wall-clock total", (long long)total_ms);
        std::fprintf(stderr, "%s\n", rule);
    }

    return 0;
}
