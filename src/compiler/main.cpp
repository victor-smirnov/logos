// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// logosc — Logos compiler driver (iteration 1).
//
// Pipeline: .logos file → PEG parser → Hermes AST → MLIR → LLVM IR → .o file.

#include "emit_module.hpp"
#include "mlir_gen.hpp"
#include "module_manifest.hpp"
#include <chrono>
#include <cstring>
#include "module_loader.hpp"
#include <logos/compiler/borrow_check.hpp>
#include <logos/compiler/ast.hpp>
#include <logos/compiler/lir.hpp>
#include <logos/compiler/mono.hpp>
#include <logos/hermes/schema_codes.hpp>

#include <logos/hermes/document.hpp>
#include <logos/hermes/mem_holder.hpp>
#include <logos/hermes/type_ops.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/access.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/arena_string.hpp>
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
#include <set>
#include <string>
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
// trivially compactable via per-document hermes::clone().
namespace {
std::set<std::string>*               g_emit_seen   = nullptr;
bool*                                g_any_emitted = nullptr;
std::vector<logos::hermes::Hermes>*  g_asts        = nullptr;
std::vector<std::string>*            g_filenames   = nullptr;
std::vector<bool>*                   g_from_binary = nullptr;
size_t                               g_user_root_idx = 0;
// Phase 7 diagnostics: hooks call logos_metaprog_error(msg) to push
// a structured error. The driver drains this after each hook and
// fails compilation if non-empty. Spans/locations TBD when hook
// arg-passing lands (an emitted node ref will carry source info).
std::vector<std::string>*            g_metaprog_diags = nullptr;
const char*                          g_current_hook_name = nullptr;
}

// Phase 7 slice 3a: read-only AST view for metaprog hooks.
//
// Returns base+size of the user's entry-file document into the
// caller's out-params. The module loader walks dependencies in
// post-order, so the entry file is at index `g_user_root_idx`
// (recorded right after load_modules, before any metaprog splice).
// Logos-side wraps the pair in `HermesView<'a>` — non-owning
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
// stdlib `std.hermes.mem_holder.MemHolder` POD, so OView treats the
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
    static_cast<logos::hermes::MemHolder*>(h)->unref();
}

// Phase 7 diagnostics: structured error from a metaprog hook.
// Copies the string (caller's buffer is hook-frame-scoped) and
// records the originating hook for the driver's report. No-op
// outside a hook frame (defensive — host accidents shouldn't crash).
extern "C" void logos_metaprog_error(const char* msg) {
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

// Phase 7 slice 13: error with span. `target_offset` is an offset
// into the user-root Hermes doc; host reads SRC_LINE from the node
// there and prefixes the diag with `<file>:<line>:`. offset==0 (no
// span) falls back to the un-spanned form.
extern "C" void logos_metaprog_error_at(uint32_t target_offset,
                                         const char* msg) {
    if (!g_metaprog_diags || !msg) return;
    std::string out;
    if (target_offset != 0
        && g_asts && g_asts->size() > g_user_root_idx
        && g_filenames && g_filenames->size() > g_user_root_idx) {
        auto* base = (*g_asts)[g_user_root_idx].holder()->base();
        auto* tom = reinterpret_cast<const logos::hermes::TinyObjectMap*>(
            base + target_offset);
        // SRC_LINE = key 24, u32 inline.
        auto line_av = tom->get(24, base);
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

extern "C" int32_t logos_emit_source(const char* src) {
    if (!g_emit_seen || !g_any_emitted || !g_asts || !g_filenames
        || !g_from_binary || !src) return 0;
    std::string s(src);
    if (!g_emit_seen->insert(s).second) return 0;  // already emitted
    logos::compiler::LogosParser parser(s);
    auto ast = parser.parse_module();
    if (ast.is_null() || !parser.at_eof()) {
        std::fprintf(stderr, "logos_emit_source: parse failed near line %u\n",
                     parser.next_line());
        g_emit_seen->erase(s);  // allow caller to retry with corrected text
        return 0;
    }
    g_asts->push_back(std::move(ast));
    g_filenames->emplace_back("<metaprog>");
    g_from_binary->push_back(false);
    *g_any_emitted = true;
    return 1;
}

// Slice 3 of metaprog-quote (~/.claude/plans/metaprog-quote.md): item-level
// splice via Hermes-bytes. The blob is a complete arena snapshot of a one-
// module Hermes document (same shape as parser output). Host reconstructs
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
    auto doc = logos::hermes::from_bytes_copy(data, size);
    if (!doc) {
        std::fprintf(stderr, "logos_emit_item_blob: from_bytes_copy failed\n");
        blob_seen.erase(key);
        return 0;
    }
    g_asts->push_back(std::move(doc).get());
    g_filenames->emplace_back("<metaprog-blob>");
    g_from_binary->push_back(false);
    *g_any_emitted = true;
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
//   { template_ptr: *const u8, template_size: u64,
//     idents_ptr: *const Ident, idents_count: u64 }
// where Ident is { ptr: *const u8, len: u64 }. POD, no relative ptrs.
extern "C" int32_t logos_emit_item_blob_subst(const void* blob_ptr) {
    if (!g_any_emitted || !g_asts || !g_filenames || !g_from_binary
        || !blob_ptr) return 0;

    namespace la = logos::compiler::ast;
    using logos::hermes::AnyVal;
    using logos::hermes::ArenaString;
    using logos::hermes::HermesAccess;
    using logos::hermes::ObjectArray;
    using logos::hermes::TinyObjectMap;
    using logos::hermes::arena_offset_t;

    struct IdentPod { const uint8_t* ptr; uint64_t len; };
    struct BlobPod {
        const uint8_t* template_ptr;
        uint64_t template_size;
        const IdentPod* const* idents_ptr;  // array of pointers to Ident
        uint64_t idents_count;
    };
    const auto* blob = reinterpret_cast<const BlobPod*>(blob_ptr);
    if (!blob->template_ptr || blob->template_size == 0) return 0;

    // Dedupe across iterations: hash template bytes plus each ident's
    // bytes. Two calls that produce identical output converge in 2 iters.
    static std::set<std::string> blob_seen;
    std::string key(reinterpret_cast<const char*>(blob->template_ptr),
                    blob->template_size);
    for (uint64_t i = 0; i < blob->idents_count; ++i) {
        const auto* idp = blob->idents_ptr[i];
        key.push_back('\x1f');  // unit-separator, can't appear in idents
        if (idp && idp->ptr && idp->len) {
            key.append(reinterpret_cast<const char*>(idp->ptr), idp->len);
        }
    }
    if (!blob_seen.insert(key).second) return 0;

    auto doc_e = logos::hermes::from_bytes_copy(blob->template_ptr,
                                                blob->template_size);
    if (!doc_e) {
        std::fprintf(stderr,
            "logos_emit_item_blob_subst: from_bytes_copy failed\n");
        blob_seen.erase(key);
        return 0;
    }
    auto doc = std::move(doc_e).get();

    // Substitute placeholders. Root is MODULE TOM with ITEMS array.
    auto& arena = HermesAccess::arena(doc);
    auto root_off = HermesAccess::root_offset(doc);
    auto root_ptr = [&]() {
        return reinterpret_cast<TinyObjectMap*>(
            HermesAccess::base(doc) + root_off.value());
    };
    if (root_ptr()->has_key(la::ITEMS.code)) {
        AnyVal items_av = root_ptr()->get(la::ITEMS.code,
                                          HermesAccess::base(doc));
        if (!items_av.is_null()) {
            auto items_off = items_av.to_offset();
            auto items_ptr = [&]() {
                return reinterpret_cast<ObjectArray*>(
                    HermesAccess::base(doc) + items_off.value());
            };
            uint64_t n = items_ptr()->size();
            for (uint64_t i = 0; i < n; ++i) {
                AnyVal it_av = items_ptr()->get(i, HermesAccess::base(doc));
                if (it_av.is_null() || !it_av.is_pointer()) continue;
                auto it_off = it_av.to_offset();
                auto item_ptr = [&]() {
                    return reinterpret_cast<TinyObjectMap*>(
                        HermesAccess::base(doc) + it_off.value());
                };
                if (!item_ptr()->has_key(la::NAME_VAR.code)) continue;
                AnyVal idx_av = item_ptr()->get(la::NAME_VAR.code,
                                                HermesAccess::base(doc));
                if (!idx_av.is_value()) {
                    std::fprintf(stderr,
                        "logos_emit_item_blob_subst: NAME_VAR not an int\n");
                    blob_seen.erase(key);
                    return 0;
                }
                int32_t idx = idx_av.as_value<int32_t>();
                if (idx < 0 || static_cast<uint64_t>(idx) >= blob->idents_count) {
                    std::fprintf(stderr,
                        "logos_emit_item_blob_subst: ident idx %d out of range (count=%llu)\n",
                        idx, (unsigned long long)blob->idents_count);
                    blob_seen.erase(key);
                    return 0;
                }
                const auto* idp = blob->idents_ptr[idx];
                if (!idp || !idp->ptr || idp->len == 0) {
                    std::fprintf(stderr,
                        "logos_emit_item_blob_subst: ident[%d] is empty\n", idx);
                    blob_seen.erase(key);
                    return 0;
                }
                auto str_e = ArenaString::create(arena,
                    std::string_view(reinterpret_cast<const char*>(idp->ptr),
                                     idp->len));
                if (!str_e) {
                    std::fprintf(stderr,
                        "logos_emit_item_blob_subst: ArenaString alloc failed\n");
                    blob_seen.erase(key);
                    return 0;
                }
                uint32_t name_off = static_cast<uint32_t>(
                    reinterpret_cast<uint8_t*>(str_e.get())
                    - HermesAccess::base(doc));
                // ArenaString::create may grow the arena; re-derive ptrs.
                (void)item_ptr()->put(la::NAME.code,
                    AnyVal::from_offset(arena_offset_t(name_off)), arena);
                item_ptr()->remove(la::NAME_VAR.code, HermesAccess::base(doc));
            }
        }
    }

    g_asts->push_back(std::move(doc));
    g_filenames->emplace_back("<metaprog-blob-subst>");
    g_from_binary->push_back(false);
    *g_any_emitted = true;
    return 1;
}

// Slice 3 test fixture: parses an inline source string and exposes the
// resulting Hermes arena bytes through out-params. The handler then calls
// logos_emit_item_blob with those bytes. Goes away once Slice 4's
// quote_item! lands and produces the same shape from in-Logos code.
namespace {
std::vector<logos::hermes::Hermes> g_test_blob_keepalive;
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

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: logosc <input.logos> [-o output.o] [-O0|-O1|-O2|-O3] [--emit-mlir] [--emit-llvm]\n");
        return 1;
    }

    // Initialize Hermes TypeOps registry; the @-literal builder uses
    // clone() which dispatches per-type via this registry.
    logos::hermes::hermes_init();

    const char* input_path = nullptr;
    const char* output_path = "output.o";
    bool emit_mlir = false;
    bool emit_llvm = false;
    bool jit_run   = false;                      // --jit: compile and run main() in-process
    const char* emit_module_manifest = nullptr;  // --emit-module <manifest>
    std::vector<std::string> search_paths;
    int opt_level = 0;

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

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) { output_path = argv[++i]; }
        else if (arg == "-I" && i + 1 < argc) { search_paths.push_back(argv[++i]); }
        else if (arg == "--emit-mlir") { emit_mlir = true; }
        else if (arg == "--emit-llvm") { emit_llvm = true; }
        else if (arg == "--jit") { jit_run = true; }
        else if (arg == "--emit-module" && i + 1 < argc) { emit_module_manifest = argv[++i]; }
        else if (arg == "-O0") { opt_level = 0; }
        else if (arg == "-O1") { opt_level = 1; }
        else if (arg == "-O2") { opt_level = 2; }
        else if (arg == "-O3") { opt_level = 3; }
        else if (arg[0] != '-' && !input_path) { input_path = argv[i]; }
    }

    // ── emit-module mode ────────────────────────────────────────────
    if (emit_module_manifest) {
        std::string err;
        auto manifest = logos::compiler::parse_module_manifest(emit_module_manifest, err);
        if (!manifest) {
            std::fprintf(stderr, "logosc: %s\n", err.c_str());
            return 1;
        }
        logos::compiler::EmitModuleOptions mopts;
        mopts.extra_search_paths = search_paths;
        mopts.emit_mlir = emit_mlir;
        mopts.emit_llvm = emit_llvm;
        return logos::compiler::emit_module(*manifest, output_path, mopts) ? 0 : 1;
    }

    const bool trace = std::getenv("LOGOS_TRACE_PHASES") != nullptr;
    auto t_start = std::chrono::steady_clock::now();
    auto report = [&](const char* label) {
        if (!trace) return;
        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - t_start).count();
        std::fprintf(stderr, "[trace %6lldms] %s\n", (long long)ms, label);
        t_start = now;
    };
    // ── Step 1-2: Load and parse all modules ────────────────────
    auto modules = logos::compiler::load_modules(input_path, search_paths);
    report("load+parse");
    if (modules.empty()) {
        std::fprintf(stderr, "logosc: no modules loaded\n");
        return 1;
    }

    // Collect ASTs and source paths.
    std::vector<logos::hermes::Hermes> asts;
    std::vector<std::string> filenames;
    std::vector<bool> from_binary;
    logos::compiler::StrSet binary_archives_seen;
    logos::compiler::StrSet binary_symbols;
    for (auto& m : modules) {
        filenames.push_back(m.path);
        from_binary.push_back(m.from_binary_module);
        asts.push_back(std::move(m.ast));
    }
    // Collect symbol tables from binary archives on the search path.
    // Shell glob + nm: avoids pulling in <filesystem> next to LLVM headers.
    // M.1 Stage 2 (Mode B): also collect the archive paths themselves so the
    // metaprog JIT can register them via StaticLibraryDefinitionGenerator.
    std::vector<std::string> archive_paths;
    for (const auto& dir : search_paths) {
        std::string cmd = "ls " + dir + "/*.a 2>/dev/null";
        if (FILE* lp = ::popen(cmd.c_str(), "r")) {
            char path[1024];
            while (std::fgets(path, sizeof(path), lp)) {
                std::string_view sv(path);
                while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r' || sv.back() == ' '))
                    sv.remove_suffix(1);
                if (!sv.empty()) archive_paths.emplace_back(sv);
            }
            ::pclose(lp);
        }
        std::string cmd2 = "nm --defined-only -j " + dir + "/*.a 2>/dev/null";
        FILE* pipe = ::popen(cmd2.c_str(), "r");
        if (!pipe) continue;
        char line[512];
        while (std::fgets(line, sizeof(line), pipe)) {
            std::string_view sv(line);
            while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r' || sv.back() == ' '))
                sv.remove_suffix(1);
            if (!sv.empty() && sv.front() != '/') // skip archive member headers like "/path/foo.o:"
                binary_symbols.emplace(sv);
        }
        ::pclose(pipe);
    }
    if (trace)
        std::fprintf(stderr, "[trace] binary_symbols: %zu from %zu archive(s)\n",
                     binary_symbols.size(), binary_archives_seen.size());

    // ── Step 2b: Semantic analysis + L-IR lowering, with metaprog loop ──
    //
    // Phase 5 driver: re-run sema after each round of post-sema hooks until
    // no new AST items are emitted.  Hooks are currently no-op C++ stubs;
    // when the JIT lands (Phase 4) and the AST emit API lands (Phase 7),
    // the body of this loop fills in.  `kMaxMetaprogIters` is a safety cap
    // against pathological recursive emission.
    constexpr int kMaxMetaprogIters = 16;
    std::set<std::string> emit_seen;
    g_emit_seen   = &emit_seen;
    g_asts        = &asts;
    g_filenames   = &filenames;
    g_from_binary = &from_binary;
    // Module loader is post-order: dependencies first, entry file last.
    // Record the entry-file index before any metaprog source-splice so
    // logos_get_module_ast keeps pointing at the user's root doc even
    // after asts grows.
    g_user_root_idx = asts.empty() ? 0 : asts.size() - 1;
    logos::compiler::lir::LProgram prog;
    // Phase 7 slice 17: pre-sema hook timing. The discovery pass runs in
    // metaprog mode — entry-file fn bodies (other than handlers) are NOT
    // lowered, so references to types/impls that hooks will synthesize do
    // not error out. Handlers and stdlib bodies *are* fully lowered, so
    // the JIT can compile them. After convergence, a final non-metaprog
    // sema pass lowers the now-complete entry file.
    logos::compiler::SemaOptions meta_opts;
    meta_opts.metaprog_mode = true;
    meta_opts.entry_ast_idx = g_user_root_idx;
    for (int iter = 0; ; ++iter) {
        prog = logos::compiler::sema_lower(asts, filenames, from_binary, meta_opts);
        prog.print_diags(stderr);
        if (!prog.ok()) return 1;
        report(iter == 0 ? "sema+lower" : "sema+lower (re-run)");

        if (prog.metaprog_targets.empty()) break;
        // Note: metacall_sites are not visible here — metaprog_mode skips
        // entry-file fn bodies, so METACALL nodes inside fn bodies are not
        // walked in this loop. Metacall handling runs in its own loop after
        // the final non-metaprog sema pass below.

        if (trace) {
            std::fprintf(stderr, "[metaprog iter %d] %zu target(s):\n",
                         iter, prog.metaprog_targets.size());
            for (auto& t : prog.metaprog_targets)
                std::fprintf(stderr, "                 - %s\n", t.trigger.c_str());
        }

        // Phase 4 slice 4: JIT-invoke each hook. Until the AST emit API
        // lands (Phase 7) hooks are validated `fn() -> ()` no-ops; we
        // call them to exercise the seam end-to-end. `meta_prog` is a
        // throwaway full-pipeline copy of the current sources — we
        // can't reuse the outer `prog` because the post-loop expects a
        // pre-mono LProgram.
        // meta_prog (JIT input) uses metaprog_mode so the entry file may
        // reference items that hooks will synthesize without erroring out.
        // After sema, drop is_metaprog_stub fns (entry-file non-handler
        // bodies were skipped) — mono/MLIR would otherwise see empty bodies.
        auto meta_prog = logos::compiler::sema_lower(asts, filenames, from_binary, meta_opts);
        if (!meta_prog.ok()) { meta_prog.print_diags(stderr); return 1; }
        meta_prog.functions.erase(
            std::remove_if(meta_prog.functions.begin(), meta_prog.functions.end(),
                [](const auto& f) { return f->is_metaprog_stub; }),
            meta_prog.functions.end());
        meta_prog = logos::compiler::reflection_emit(std::move(meta_prog));
        meta_prog = logos::compiler::mono_pass(std::move(meta_prog));
        if (!meta_prog.ok()) { meta_prog.print_diags(stderr); return 1; }
        meta_prog = logos::compiler::borrow_check(std::move(meta_prog));
        if (!meta_prog.ok()) { meta_prog.print_diags(stderr); return 1; }

        mlir::MLIRContext meta_mlir_ctx;
        meta_mlir_ctx.getOrLoadDialect<mlir::func::FuncDialect>();
        meta_mlir_ctx.getOrLoadDialect<mlir::arith::ArithDialect>();
        meta_mlir_ctx.getOrLoadDialect<mlir::scf::SCFDialect>();
        meta_mlir_ctx.getOrLoadDialect<mlir::cf::ControlFlowDialect>();
        meta_mlir_ctx.getOrLoadDialect<mlir::LLVM::LLVMDialect>();
        auto meta_mlir = logos::compiler::mlir_gen(meta_mlir_ctx, meta_prog);
        if (!meta_mlir) { std::fprintf(stderr, "logosc: metaprog MLIR gen failed\n"); return 1; }
        mlir::PassManager meta_pm(&meta_mlir_ctx);
        meta_pm.addPass(mlir::createSCFToControlFlowPass());
        meta_pm.addPass(mlir::createConvertControlFlowToLLVMPass());
        meta_pm.addPass(mlir::createArithToLLVMConversionPass());
        meta_pm.addPass(mlir::createConvertFuncToLLVMPass());
        meta_pm.addPass(mlir::createReconcileUnrealizedCastsPass());
        if (mlir::failed(meta_pm.run(*meta_mlir))) {
            std::fprintf(stderr, "logosc: metaprog MLIR lowering failed\n"); return 1;
        }
        mlir::registerBuiltinDialectTranslation(meta_mlir_ctx);
        mlir::registerLLVMDialectTranslation(meta_mlir_ctx);
        llvm::LLVMContext meta_llvm_ctx;
        auto meta_llvm = mlir::translateModuleToLLVMIR(*meta_mlir, meta_llvm_ctx);
        if (!meta_llvm) { std::fprintf(stderr, "logosc: metaprog LLVM IR translate failed\n"); return 1; }
        meta_llvm->setTargetTriple(llvm::Triple(llvm::sys::getDefaultTargetTriple()));
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();

        // Metaprog JIT gets process symbols (libc malloc/free, printf,
        // memcpy, etc.) so hooks can use stdlib types like String/format
        // without per-symbol bindings. The compiler is the trust root
        // for hook code, so this is acceptable.
        auto meta_jit = build_jit_from_module(*meta_llvm, "logosc-metaprog",
                                              /*with_process_symbols=*/true,
                                              &archive_paths);
        if (!meta_jit) return 1;
        report("metaprog jit");

        bool any_emitted = false;
        g_any_emitted = &any_emitted;
        std::vector<std::string> hook_diags;
        g_metaprog_diags = &hook_diags;
        if (!meta_jit->define_symbol("logos_emit_source",
                                     reinterpret_cast<void*>(&logos_emit_source))) {
            std::fprintf(stderr, "logosc: bind logos_emit_source: %s\n",
                         meta_jit->error_str().c_str());
            return 1;
        }
        if (!meta_jit->define_symbol("logos_emit_item_blob",
                                     reinterpret_cast<void*>(&logos_emit_item_blob))) {
            std::fprintf(stderr, "logosc: bind logos_emit_item_blob: %s\n",
                         meta_jit->error_str().c_str());
            return 1;
        }
        if (!meta_jit->define_symbol("logos_emit_item_blob_subst",
                                     reinterpret_cast<void*>(&logos_emit_item_blob_subst))) {
            std::fprintf(stderr, "logosc: bind logos_emit_item_blob_subst: %s\n",
                         meta_jit->error_str().c_str());
            return 1;
        }
        if (!meta_jit->define_symbol("logos_metaprog_test_module_blob",
                                     reinterpret_cast<void*>(&logos_metaprog_test_module_blob))) {
            std::fprintf(stderr, "logosc: bind logos_metaprog_test_module_blob: %s\n",
                         meta_jit->error_str().c_str());
            return 1;
        }
        if (!meta_jit->define_symbol("logos_get_module_ast",
                                     reinterpret_cast<void*>(&logos_get_module_ast))) {
            std::fprintf(stderr, "logosc: bind logos_get_module_ast: %s\n",
                         meta_jit->error_str().c_str());
            return 1;
        }
        if (!meta_jit->define_symbol("logos_get_module_ast_oview",
                                     reinterpret_cast<void*>(&logos_get_module_ast_oview))) {
            std::fprintf(stderr, "logosc: bind logos_get_module_ast_oview: %s\n",
                         meta_jit->error_str().c_str());
            return 1;
        }
        if (!meta_jit->define_symbol("logos_holder_release",
                                     reinterpret_cast<void*>(&logos_holder_release))) {
            std::fprintf(stderr, "logosc: bind logos_holder_release: %s\n",
                         meta_jit->error_str().c_str());
            return 1;
        }
        if (!meta_jit->define_symbol("logos_metaprog_error",
                                     reinterpret_cast<void*>(&logos_metaprog_error))) {
            std::fprintf(stderr, "logosc: bind logos_metaprog_error: %s\n",
                         meta_jit->error_str().c_str());
            return 1;
        }
        if (!meta_jit->define_symbol("logos_metaprog_error_at",
                                     reinterpret_cast<void*>(&logos_metaprog_error_at))) {
            std::fprintf(stderr, "logosc: bind logos_metaprog_error_at: %s\n",
                         meta_jit->error_str().c_str());
            return 1;
        }
        // Phase 7 slice 12: derive-style handlers fire once per target item.
        // trigger→hook lookup is linear (handler list is short — typically
        // a handful per program); switch to map if it grows.
        for (const auto& tgt : prog.metaprog_targets) {
            // Offset is only meaningful within one Hermes doc; hooks
            // see the entry-file doc via `oview_module_ast()`. Skip
            // triggers in non-entry asts (imported user sources):
            // cross-doc targeting needs an ast_idx-aware hook ABI.
            if (tgt.ast_idx != g_user_root_idx) continue;
            // Phase 7 slice 14: fire all handlers registered for this
            // trigger, in source-declaration order (the order sema
            // collected them).
            bool any_handler = false;
            for (const auto& mh : prog.metaprog_handlers) {
                if (mh.trigger != tgt.trigger) continue;
                any_handler = true;
                auto* sym = meta_jit->lookup(mh.hook_fn);
                if (!sym) {
                    std::fprintf(stderr, "logosc: metaprog hook lookup '%s': %s\n",
                                 mh.hook_fn.c_str(), meta_jit->error_str().c_str());
                    return 1;
                }
                g_current_hook_name = mh.hook_fn.c_str();
                reinterpret_cast<void (*)(uint32_t)>(sym)(tgt.item_offset);
                g_current_hook_name = nullptr;
            }
            if (!any_handler) {
                std::fprintf(stderr,
                    "logosc: internal: no handler for trigger '%s'\n",
                    tgt.trigger.c_str());
                return 1;
            }
        }
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

        // TODO Phase 5 (compactify): for each AST, asts[i] = hermes::clone(asts[i]);
        // — drops dead nodes accumulated by the previous iteration.
    }
    // Phase 7 slice 17: final, non-metaprog sema pass. Discovery loop ran in
    // metaprog_mode which skips entry-file fn bodies; here we lower them
    // for real, now that all hook-synthesized items are present.
    //
    // M.1 Stage 2: this sema pass is also where metacall sites are first
    // discovered (metaprog_mode skipped fn bodies, so METACALL was never
    // visited in the loop above). Sites carry synthesised thunk source;
    // we run a small inner loop here to compile + invoke + splice the
    // thunks, then re-run sema until no METACALL remains.
    prog = logos::compiler::sema_lower(asts, filenames, from_binary);
    prog.print_diags(stderr);
    if (!prog.ok()) return 1;

    {
        // logos_emit_source requires g_any_emitted alive; the metaprog loop's
        // local has gone out of scope by now, so wire up a fresh one for the
        // metacall splice phase.
        bool mc_any_emitted = false;
        g_any_emitted = &mc_any_emitted;
        constexpr int kMaxMetacallIters = 16;
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
                if (site.thunk_source.empty()) continue;
                if (logos_emit_source(site.thunk_source.c_str()))
                    emitted_any_thunk = true;
            }
            if (emitted_any_thunk) {
                // Re-sema so the JIT module below picks up the new thunks.
                prog = logos::compiler::sema_lower(asts, filenames, from_binary);
                prog.print_diags(stderr);
                if (!prog.ok()) return 1;
            }

            // Step 2: full pipeline through JIT for the metacall thunks.
            auto mc_prog = logos::compiler::sema_lower(asts, filenames, from_binary);
            if (!mc_prog.ok()) { mc_prog.print_diags(stderr); return 1; }
            mc_prog = logos::compiler::reflection_emit(std::move(mc_prog));
            mc_prog = logos::compiler::mono_pass(std::move(mc_prog));
            if (!mc_prog.ok()) { mc_prog.print_diags(stderr); return 1; }
            mc_prog = logos::compiler::borrow_check(std::move(mc_prog));
            if (!mc_prog.ok()) { mc_prog.print_diags(stderr); return 1; }

            mlir::MLIRContext mc_ctx;
            mc_ctx.getOrLoadDialect<mlir::func::FuncDialect>();
            mc_ctx.getOrLoadDialect<mlir::arith::ArithDialect>();
            mc_ctx.getOrLoadDialect<mlir::scf::SCFDialect>();
            mc_ctx.getOrLoadDialect<mlir::cf::ControlFlowDialect>();
            mc_ctx.getOrLoadDialect<mlir::LLVM::LLVMDialect>();
            auto mc_mlir = logos::compiler::mlir_gen(mc_ctx, mc_prog);
            if (!mc_mlir) { std::fprintf(stderr, "logosc: metacall MLIR gen failed\n"); return 1; }
            mlir::PassManager mc_pm(&mc_ctx);
            mc_pm.addPass(mlir::createSCFToControlFlowPass());
            mc_pm.addPass(mlir::createConvertControlFlowToLLVMPass());
            mc_pm.addPass(mlir::createArithToLLVMConversionPass());
            mc_pm.addPass(mlir::createConvertFuncToLLVMPass());
            mc_pm.addPass(mlir::createReconcileUnrealizedCastsPass());
            if (mlir::failed(mc_pm.run(*mc_mlir))) {
                std::fprintf(stderr, "logosc: metacall MLIR lowering failed\n"); return 1;
            }
            mlir::registerBuiltinDialectTranslation(mc_ctx);
            mlir::registerLLVMDialectTranslation(mc_ctx);
            llvm::LLVMContext mc_llvm_ctx;
            auto mc_llvm = mlir::translateModuleToLLVMIR(*mc_mlir, mc_llvm_ctx);
            if (!mc_llvm) { std::fprintf(stderr, "logosc: metacall LLVM IR translate failed\n"); return 1; }
            mc_llvm->setTargetTriple(llvm::Triple(llvm::sys::getDefaultTargetTriple()));
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

            // Step 3: invoke each thunk and splice the result into the AST.
            using RT = logos::compiler::lir::LProgram::MetacallSite::RetTag;
            bool any_spliced = false;
            for (const auto& site : prog.metacall_sites) {
                if (site.thunk_source.empty()) continue;
                if (site.ast_idx >= asts.size()) continue;
                auto* sym = mc_jit->lookup(site.thunk_name);
                if (!sym) {
                    std::fprintf(stderr, "logosc: metacall thunk lookup '%s': %s\n",
                                 site.thunk_name.c_str(), mc_jit->error_str().c_str());
                    return 1;
                }

                int64_t  i_val = 0;
                uint64_t u_val = 0;
                double   f_val = 0.0;
                bool     b_val = false;
                std::string s_val;
                std::string blob_bytes;  // for HermesStatic ret
                bool is_float = false, is_bool = false, is_str = false, is_unsigned = false;
                bool is_hermes_blob = false;
                switch (site.ret_tag) {
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
                case RT::HermesStatic: {
                    // HermesStatic = { ptr: *const u8 }, DataPlain ≤ 16B, returned in rax.
                    // Layout in meta-jit rodata: [u64 size_le][bytes]; ptr points past the prefix.
                    auto blob_ptr = reinterpret_cast<const uint8_t* (*)()>(sym)();
                    if (!blob_ptr) {
                        std::fprintf(stderr, "logosc: metacall HermesStatic thunk returned null\n");
                        return 1;
                    }
                    uint64_t size = 0;
                    std::memcpy(&size, blob_ptr - 8, 8);
                    blob_bytes.assign(reinterpret_cast<const char*>(blob_ptr), size);
                    is_hermes_blob = true;
                    break;
                }
                case RT::Hermes: {
                    // Hermes-returning thunk wraps callee in __metacall_freeze, which
                    // mallocs [u64 size][bytes] and returns ptr past prefix — same ABI
                    // as HermesStatic from this side.
                    auto blob_ptr = reinterpret_cast<const uint8_t* (*)()>(sym)();
                    if (!blob_ptr) {
                        std::fprintf(stderr, "logosc: metacall Hermes thunk returned null\n");
                        return 1;
                    }
                    uint64_t size = 0;
                    std::memcpy(&size, blob_ptr - 8, 8);
                    blob_bytes.assign(reinterpret_cast<const char*>(blob_ptr), size);
                    is_hermes_blob = true;
                    break;
                }
                }

                std::string lit_text;
                int32_t new_code = logos::compiler::ast::LIT_INT;
                if (is_bool) { lit_text = b_val ? "true" : "false"; new_code = logos::compiler::ast::LIT_BOOL; }
                else if (is_hermes_blob) { lit_text = blob_bytes; new_code = logos::compiler::ast::HERMES_BLOB; }
                else if (is_str) { lit_text = s_val; new_code = logos::compiler::ast::LIT_STR; }
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

                auto& doc = asts[site.ast_idx];
                auto* h = doc.holder();
                auto* base = h->base();
                auto* tom = reinterpret_cast<logos::hermes::TinyObjectMap*>(base + site.expr_offset);

                logos::hermes::AnyVal value_av;
                if (is_bool) {
                    value_av = logos::hermes::AnyVal::from_value<uint8_t>(b_val ? 1 : 0);
                } else {
                    auto str_exp = doc.make_string(lit_text);
                    if (!str_exp) {
                        std::fprintf(stderr, "logosc: metacall splice: make_string OOM\n");
                        return 1;
                    }
                    value_av = str_exp->to_anyval();
                    base = h->base();
                    tom = reinterpret_cast<logos::hermes::TinyObjectMap*>(base + site.expr_offset);
                }

                if (auto r = tom->put(logos::compiler::ast::CODE.code,
                                      logos::hermes::AnyVal::from_value<int32_t>(new_code),
                                      h->arena()); !r) {
                    std::fprintf(stderr, "logosc: metacall splice: CODE put failed\n");
                    return 1;
                }
                base = h->base();
                tom = reinterpret_cast<logos::hermes::TinyObjectMap*>(base + site.expr_offset);
                tom->set_schema_type_code(
                    logos::hermes::schema::ast(static_cast<int32_t>(new_code)));
                if (auto r = tom->put(logos::compiler::ast::VALUE.code, value_av, h->arena()); !r) {
                    std::fprintf(stderr, "logosc: metacall splice: VALUE put failed\n");
                    return 1;
                }
                any_spliced = true;
            }

            // Step 4: re-run sema. Sites should disappear (METACALL→LIT_INT).
            // Loop continues only if new sites somehow appeared.
            (void)any_spliced;
            prog = logos::compiler::sema_lower(asts, filenames, from_binary);
            prog.print_diags(stderr);
            if (!prog.ok()) return 1;
        }
    }
    report("sema+lower (final)");

    // Phase 7 slice 19: strip metaprog hook fns from the FINAL prog. Their
    // bodies reference host-only symbols (logos_emit_source, etc.) that the
    // user's compiled artifact has no business carrying — they're purely
    // compile-time machinery. Hooks are still validated (sema lowered them)
    // but won't reach mono / MLIR / linker.
    {
        std::set<std::string> hook_names;
        for (const auto& mh : prog.metaprog_handlers) hook_names.insert(mh.hook_fn);
        prog.functions.erase(
            std::remove_if(prog.functions.begin(), prog.functions.end(),
                [&](const auto& f) { return hook_names.count(f->name) > 0; }),
            prog.functions.end());
    }

    prog.binary_symbols = std::move(binary_symbols);

    // ── Step 2b+: Reflection TypeInfo emission (pre-mono, concrete types only)
    prog = logos::compiler::reflection_emit(std::move(prog));
    report("reflection_emit");

    // ── Step 2c: Monomorphization (also emits L-IR Hermes mirror) ─
    prog = logos::compiler::mono_pass(std::move(prog));
    prog.print_diags(stderr);
    if (!prog.ok()) return 1;
    report("mono");

    // ── Step 2d: Borrow checking ─────────────────────────────────
    prog = logos::compiler::borrow_check(std::move(prog));
    prog.print_diags(stderr);
    if (!prog.ok()) return 1;
    report("borrow");

    // ── Step 3: L-IR → MLIR ─────────────────────────────────────
    mlir::MLIRContext mlir_ctx;
    mlir_ctx.getOrLoadDialect<mlir::func::FuncDialect>();
    mlir_ctx.getOrLoadDialect<mlir::arith::ArithDialect>();
    mlir_ctx.getOrLoadDialect<mlir::scf::SCFDialect>();
    mlir_ctx.getOrLoadDialect<mlir::cf::ControlFlowDialect>();
    mlir_ctx.getOrLoadDialect<mlir::LLVM::LLVMDialect>();

    auto mlir_module = logos::compiler::mlir_gen(mlir_ctx, prog);
    report("mlir_gen");
    if (std::getenv("LOGOS_DUMP_MLIR")) mlir_module->dump();
    if (!mlir_module) {
        std::fprintf(stderr, "logosc: MLIR generation failed\n");
        return 1;
    }

    if (emit_mlir) {
        mlir_module->dump();
        return 0;
    }

    // ── Step 4: MLIR → LLVM dialect ─────────────────────────────
    if (std::getenv("LOGOS_DUMP_MLIR")) mlir_module->dump();
    mlir::PassManager pm(&mlir_ctx);
    pm.addPass(mlir::createSCFToControlFlowPass());
    pm.addPass(mlir::createConvertControlFlowToLLVMPass());
    pm.addPass(mlir::createArithToLLVMConversionPass());
    pm.addPass(mlir::createConvertFuncToLLVMPass());
    pm.addPass(mlir::createReconcileUnrealizedCastsPass());

    if (mlir::failed(pm.run(*mlir_module))) {
        std::fprintf(stderr, "logosc: MLIR lowering failed\n");
        return 1;
    }
    report("mlir->llvm dialect");

    // ── Step 5: MLIR LLVM dialect → LLVM IR ─────────────────────
    mlir::registerBuiltinDialectTranslation(mlir_ctx);
    mlir::registerLLVMDialectTranslation(mlir_ctx);

    llvm::LLVMContext llvm_ctx;
    auto llvm_module = mlir::translateModuleToLLVMIR(*mlir_module, llvm_ctx);
    report("llvm_ir");
    if (!llvm_module) {
        std::fprintf(stderr, "logosc: LLVM IR translation failed\n");
        return 1;
    }

    llvm_module->setTargetTriple(llvm::Triple(llvm::sys::getDefaultTargetTriple()));

    // ── --jit: compile and run main() in-process via ORC ──────────────
    if (jit_run) {
        auto jit = build_jit_from_module(*llvm_module, "logosc");
        if (!jit) return 1;
        auto* sym = jit->lookup("main");
        if (!sym) {
            std::fprintf(stderr, "logosc: jit lookup main: %s\n",
                         jit->error_str().c_str());
            return 1;
        }
        report("jit_compile");
        return reinterpret_cast<int (*)()>(sym)();
    }

    if (emit_llvm) {
        llvm_module->print(llvm::outs(), nullptr);
        return 0;
    }

    // ── Step 6: LLVM IR → object file ───────────────────────────
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    report("llvm_init");

    std::string error;
    auto* target = llvm::TargetRegistry::lookupTarget(
        llvm_module->getTargetTriple(), error);
    if (!target) {
        std::fprintf(stderr, "logosc: target lookup failed: %s\n", error.c_str());
        return 1;
    }

    // Map opt_level → LLVM enums.
    auto llvm_opt = [&]() -> llvm::CodeGenOptLevel {
        switch (opt_level) {
            case 1: return llvm::CodeGenOptLevel::Less;
            case 2: return llvm::CodeGenOptLevel::Default;
            case 3: return llvm::CodeGenOptLevel::Aggressive;
            default: return llvm::CodeGenOptLevel::None;
        }
    }();
    auto pb_opt = [&]() -> llvm::OptimizationLevel {
        switch (opt_level) {
            case 1: return llvm::OptimizationLevel::O1;
            case 2: return llvm::OptimizationLevel::O2;
            case 3: return llvm::OptimizationLevel::O3;
            default: return llvm::OptimizationLevel::O0;
        }
    }();

    auto target_machine = std::unique_ptr<llvm::TargetMachine>(
        target->createTargetMachine(
            llvm_module->getTargetTriple(), "generic", "",
            llvm::TargetOptions{}, llvm::Reloc::PIC_,
            std::nullopt, llvm_opt));

    llvm_module->setDataLayout(target_machine->createDataLayout());
    report("target_machine");

    // ── Step 6b: run LLVM optimization pipeline (O1/O2/O3 only) ────────────
    if (opt_level > 0) {
        llvm::LoopAnalysisManager     lam;
        llvm::FunctionAnalysisManager fam;
        llvm::CGSCCAnalysisManager    cgam;
        llvm::ModuleAnalysisManager   mam;

        llvm::PassBuilder pb(target_machine.get());
        // Register all standard analyses.
        pb.registerModuleAnalyses(mam);
        pb.registerCGSCCAnalyses(cgam);
        pb.registerFunctionAnalyses(fam);
        pb.registerLoopAnalyses(lam);
        pb.crossRegisterProxies(lam, fam, cgam, mam);

        auto mpm = pb.buildPerModuleDefaultPipeline(pb_opt);
        mpm.run(*llvm_module, mam);
        report("opt");
    }

    std::error_code ec;
    llvm::raw_fd_ostream out(output_path, ec, llvm::sys::fs::OF_None);
    if (ec) {
        std::fprintf(stderr, "logosc: cannot open output '%s': %s\n",
                     output_path, ec.message().c_str());
        return 1;
    }

    llvm::legacy::PassManager pass;
    if (target_machine->addPassesToEmitFile(pass, out, nullptr,
                                             llvm::CodeGenFileType::ObjectFile)) {
        std::fprintf(stderr, "logosc: target cannot emit object file\n");
        return 1;
    }

    pass.run(*llvm_module);
    out.flush();
    report("codegen+write");

    std::fprintf(stderr, "logosc: wrote %s\n", output_path);

    // Measure destructor/dealloc time for large objects.
    {
        auto t0 = std::chrono::steady_clock::now();
        llvm_module.reset();
        mlir_module = {};
        if (trace) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
            std::fprintf(stderr, "[trace   %4lldms] destroy\n", (long long)ms);
        }
    }
    return 0;
}
