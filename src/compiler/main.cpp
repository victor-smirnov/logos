// Logos project — https://github.com/victor-smirnov/logos
//
// logosc — Logos compiler driver (iteration 1).
//
// Pipeline: .logos file → PEG parser → Hermes AST → MLIR → LLVM IR → .o file.

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
#include <logos/compiler/mono.hpp>
#include <logos/hermes/compat.hpp>

#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp>
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
#include <unordered_map>
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
// `&blob[8]` (past the size prefix) to match the ExprBlob/HermesStatic ABI.
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
// whole new Hermes document per emission; per-item stamping would
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
        auto tom = logos::hermes::TinyMapView(
            logos::hermes::arena_offset_t(target_offset),
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
    if (g_module_ids) g_module_ids->push_back(g_self_module_id);
    *g_any_emitted = true;
    record_emit_provenance();
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
    using logos::hermes::AnyVal;
    using logos::hermes::ArenaString;
    using logos::hermes::HermesAccess;
    using logos::hermes::ObjectArray;
    using logos::hermes::TinyObjectMap;
    using logos::hermes::TinyMapView;
    using logos::hermes::ArrayView;
    using logos::hermes::MapView;
    using logos::hermes::as_tinymap;
    using logos::hermes::as_array;
    using logos::hermes::arena_offset_t;
    using logos::hermes::copy_object_into;

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

    auto doc_e = logos::hermes::from_bytes_copy(blob->template_ptr,
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
        size_t bound = static_cast<size_t>(blob->template_size)
                         * (2 * total_idents + 2)
                     + static_cast<size_t>(total_str) + 65536;
        (void)HermesAccess::arena(doc).reserve(bound);
    }

    // Substitute placeholders. Root is MODULE TOM with ITEMS array.
    auto& arena = HermesAccess::arena(doc);
    auto root_off = HermesAccess::root_offset(doc);
    auto root_ptr = [&]() {
        return TinyMapView(arena_offset_t(root_off.value()), doc.holder());
    };
    // Recursive walker: every TOM with NAME_VAR(int idx) gets the slot
    // replaced by NAME(string) drawn from idents[idx]. Mirrors the dst
    // walker in lower_quote_item, so placeholders at any nesting depth
    // (struct field name, fn arg name, type ref name, …) are resolved.
    namespace lh = logos::hermes;
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
        uint8_t* dbase = HermesAccess::base(doc);
        auto ctom = logos::hermes::TinyMapView(arena_offset_t(child_off), doc.holder());
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
        auto inner_e = logos::hermes::from_bytes_copy(
            blob->blobs_blob + be.offset, be.size);
        if (!inner_e) {
            std::fprintf(stderr,
                "logos_emit_item_blob_subst: blob[%llu] from_bytes_copy failed\n",
                (unsigned long long)bidx);
            subst_failed = true; return true;
        }
        auto inner_doc = std::move(inner_e).get();
        const uint8_t* ib = HermesAccess::base(inner_doc);
        auto inner_root_off = HermesAccess::root_offset(inner_doc).value();
        const void* inner_root = ib + inner_root_off;
        auto cp_e = copy_object_into(inner_root, ib, doc);
        if (!cp_e) {
            std::fprintf(stderr,
                "logos_emit_item_blob_subst: copy_object_into failed\n");
            subst_failed = true; return true;
        }
        void* dst_obj = cp_e.get();
        uint32_t dst_off = static_cast<uint32_t>(
            reinterpret_cast<uint8_t*>(dst_obj) - HermesAccess::base(doc));
        replace_in_parent(dst_off);
        return true;
    };

    // Cursor expansion (item-level REPEAT_GROUP): substitutes
    // NAME_VAR(int with bit 30 set) → NAME(cursor_hdrs[idx].pods[iter])
    // throughout a freshly cloned subtree. Cursor placeholders only —
    // ident/blob placeholders in the body remain for the outer subst_walk.
    std::function<void(uint32_t, uint64_t)> expand_cursor_in_subtree;
    expand_cursor_in_subtree = [&](uint32_t off, uint64_t iter) {
        uint8_t* dbase = HermesAccess::base(doc);
        auto tom = logos::hermes::TinyMapView(arena_offset_t(off), doc.holder());
        if (tom.has_key(la::NAME_VAR.code)) {
            AnyVal nv = tom.get(la::NAME_VAR.code);
            if (nv.is_value()) {
                int32_t enc = nv.as_value<int32_t>();
                if (enc >= 0 && (enc & 0x400000) != 0) {
                    // Encoding: bit 22 = is-cursor, bits 0-7 = cursor index,
                    // bits 8-21 = pinned-outer-index + 1 (0 = unpinned).
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
                                dbase = HermesAccess::base(doc);
                                tom = logos::hermes::TinyMapView(arena_offset_t(off), doc.holder());
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
                                    AnyVal::from_offset(HermesAccess::base(doc), arena_offset_t(name_off)));
                                dbase = HermesAccess::base(doc);
                                tom = logos::hermes::TinyMapView(arena_offset_t(off), doc.holder());
                                tom.remove(la::NAME_VAR.code);
                            }
                        }
                    }
                }
            }
        }
        // Snapshot children before recursion (puts may have rebased).
        std::vector<std::pair<bool, uint32_t>> children;
        dbase = HermesAccess::base(doc);
        tom = logos::hermes::TinyMapView(arena_offset_t(off), doc.holder());
        uint64_t bm = tom.bitmap();
        for (uint8_t key = 0; key < TinyObjectMap::MAX_KEYS; ++key) {
            if (!(bm & (1ULL << key))) continue;
            if (key == la::NAME_VAR.code) continue;
            AnyVal av = tom.get(key);
            if (av.is_null() || !av.is_pointer()) continue;
            uint32_t coff = static_cast<uint32_t>(av.to_offset(HermesAccess::base(doc)).value());
            const uint8_t* pointee = dbase + coff;
            lh::TypeTag tag = lh::TypeTag::read_before(pointee);
            if (tag.type_code() == lh::type_hash::TinyObjectMap)
                children.push_back({false, coff});
            else if (tag.type_code() == lh::type_hash::Array)
                children.push_back({true, coff});
        }
        for (auto [is_arr, coff] : children) {
            if (!is_arr) {
                expand_cursor_in_subtree(coff, iter);
                continue;
            }
            uint8_t* db2 = HermesAccess::base(doc);
            auto arr = logos::hermes::ArrayView(arena_offset_t(coff), doc.holder());
            std::vector<uint32_t> elem_offs;
            for (uint64_t i = 0; i < arr.size(); ++i) {
                AnyVal e = arr.get(i);
                if (e.is_null() || !e.is_pointer()) continue;
                uint32_t eoff = static_cast<uint32_t>(e.to_offset(HermesAccess::base(doc)).value());
                const uint8_t* ep = db2 + eoff;
                lh::TypeTag etag = lh::TypeTag::read_before(ep);
                if (etag.type_code() == lh::type_hash::TinyObjectMap)
                    elem_offs.push_back(eoff);
            }
            for (uint32_t eoff : elem_offs)
                expand_cursor_in_subtree(eoff, iter);
        }
    };

    // Find the cursor count to expand a REPEAT_GROUP body to. Walks the
    // body looking for any cursor-encoded NAME_VAR; uses cursor_hdrs[idx].count.
    // Errors if no cursor found (caller validated this at sema time).
    std::function<uint64_t(uint32_t)> find_cursor_count_in_body;
    find_cursor_count_in_body = [&](uint32_t off) -> uint64_t {
        uint8_t* dbase = HermesAccess::base(doc);
        auto tom = logos::hermes::TinyMapView(arena_offset_t(off), doc.holder());
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
            uint32_t coff = static_cast<uint32_t>(av.to_offset(HermesAccess::base(doc)).value());
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
                    uint32_t eoff = static_cast<uint32_t>(e.to_offset(HermesAccess::base(doc)).value());
                    const uint8_t* ep = HermesAccess::base(doc) + eoff;
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
        uint8_t* dbase = HermesAccess::base(doc);
        auto arr = logos::hermes::ArrayView(arena_offset_t(arr_off), doc.holder());
        bool any_repeat = false;
        uint64_t n_src = arr.size();
        for (uint64_t i = 0; i < n_src; ++i) {
            AnyVal e = arr.get(i);
            if (!e.is_pointer()) continue;
            uint32_t eoff = static_cast<uint32_t>(e.to_offset(HermesAccess::base(doc)).value());
            auto etom = logos::hermes::TinyMapView(arena_offset_t(eoff), doc.holder());
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
            uint32_t eoff = static_cast<uint32_t>(e.to_offset(HermesAccess::base(doc)).value());
            auto etom = logos::hermes::TinyMapView(arena_offset_t(eoff), doc.holder());
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
                    bav.to_offset(HermesAccess::base(doc)).value()});
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
                        AnyVal::from_offset(HermesAccess::base(doc), arena_offset_t(src_els[i].off))
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
                const uint8_t* base = HermesAccess::base(doc);
                const void* src_obj = base + body_off;
                auto cp_e = copy_object_into(src_obj, base, doc);
                if (!cp_e) { subst_failed = true; return 0; }
                void* dst_obj = cp_e.get();
                uint32_t copy_off = static_cast<uint32_t>(
                    reinterpret_cast<uint8_t*>(dst_obj) - HermesAccess::base(doc));
                expand_cursor_in_subtree(copy_off, j);
                auto na2 = ArrayView(arena_offset_t(new_arr_off), doc.holder());
                (void)na2.push_back(
                    AnyVal::from_offset(HermesAccess::base(doc), arena_offset_t(copy_off)));
            }
        }
        return new_arr_off;
    };

    std::function<void(uint32_t)> subst_walk = [&](uint32_t off) {
        if (subst_failed) return;
        uint8_t* dbase = HermesAccess::base(doc);
        auto tom = logos::hermes::TinyMapView(arena_offset_t(off), doc.holder());
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
                dbase = HermesAccess::base(doc);
                tom = logos::hermes::TinyMapView(arena_offset_t(off), doc.holder());
                // `##name` antiquot parses as a LIT_STR node carrying
                // NAME_VAR; the substituted ident text is the string's
                // VALUE (a string-literal label), not its NAME. Plain
                // `#name` (VAR_REF) takes NAME. (T2-22 str-position antiquot.)
                bool is_strlit = false;
                if (tom.has_key(la::CODE.code)) {
                    AnyVal cv = tom.get(la::CODE.code);
                    if (cv.is_value() && cv.as_value<int32_t>() == la::LIT_STR.code)
                        is_strlit = true;
                }
                (void)tom.put(is_strlit ? la::VALUE.code : la::NAME.code,
                    AnyVal::from_offset(HermesAccess::base(doc), arena_offset_t(name_off)));
                dbase = HermesAccess::base(doc);
                tom = logos::hermes::TinyMapView(arena_offset_t(off), doc.holder());
                tom.remove(la::NAME_VAR.code);
            }
        }
        // Snapshot children before recursion (puts above may have rebased).
        // Track key for TOM children and elem-index for array children so
        // blob splice can rewrite the parent's slot.
        struct ChildRef { bool is_arr; uint8_t key; uint32_t coff; };
        std::vector<ChildRef> children;
        dbase = HermesAccess::base(doc);
        tom = logos::hermes::TinyMapView(arena_offset_t(off), doc.holder());
        uint64_t bm = tom.bitmap();
        for (uint8_t key = 0; key < TinyObjectMap::MAX_KEYS; ++key) {
            if (!(bm & (1ULL << key))) continue;
            if (key == la::NAME_VAR.code) continue;
            AnyVal av = tom.get(key);
            if (av.is_null() || !av.is_pointer()) continue;
            uint32_t coff = static_cast<uint32_t>(av.to_offset(HermesAccess::base(doc)).value());
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
                        uint8_t* db = HermesAccess::base(doc);
                        auto t = logos::hermes::TinyMapView(arena_offset_t(off), doc.holder());
                        (void)t.put(key,
                            AnyVal::from_offset(HermesAccess::base(doc), arena_offset_t(new_off)));
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
                    uint8_t* db = HermesAccess::base(doc);
                    auto t = logos::hermes::TinyMapView(arena_offset_t(off), doc.holder());
                    (void)t.put(cref.key,
                        AnyVal::from_offset(HermesAccess::base(doc), arena_offset_t(expanded)));
                    arr_off = expanded;
                }
            }
            uint8_t* dbase2 = HermesAccess::base(doc);
            auto arr = logos::hermes::ArrayView(arena_offset_t(arr_off), doc.holder());
            std::vector<std::pair<uint64_t, uint32_t>> elems;
            for (uint64_t i = 0; i < arr.size(); ++i) {
                AnyVal e = arr.get(i);
                if (e.is_null() || !e.is_pointer()) continue;
                uint32_t eoff = static_cast<uint32_t>(e.to_offset(HermesAccess::base(doc)).value());
                const uint8_t* ep = dbase2 + eoff;
                lh::TypeTag etag = lh::TypeTag::read_before(ep);
                if (etag.type_code() == lh::type_hash::TinyObjectMap)
                    elems.push_back({i, eoff});
            }
            for (auto [ei, eoff] : elems) {
                if (subst_failed) return;
                bool spliced = try_blob_splice(eoff,
                    [&](uint32_t new_off) {
                        uint8_t* db = HermesAccess::base(doc);
                        auto a = logos::hermes::ArrayView(arena_offset_t(arr_off), doc.holder());
                        a.set(ei,
                               AnyVal::from_offset(HermesAccess::base(doc), arena_offset_t(new_off))
                               );
                    });
                if (!spliced) subst_walk(eoff);
            }
        }
    };
    if (root_ptr().has_key(la::ITEMS.code)) {
        AnyVal items_av = root_ptr().get(la::ITEMS.code);
        if (!items_av.is_null()) {
            auto items_off = items_av.to_offset(HermesAccess::base(doc));
            auto items_ptr = [&]() {
                return ArrayView(arena_offset_t(items_off.value()), doc.holder());
            };
            uint64_t n = items_ptr().size();
            for (uint64_t i = 0; i < n; ++i) {
                AnyVal it_av = items_ptr().get(i);
                if (it_av.is_null() || !it_av.is_pointer()) continue;
                subst_walk(static_cast<uint32_t>(it_av.to_offset(HermesAccess::base(doc)).value()));
                if (subst_failed) {
                    blob_seen.erase(key); return 0;
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
                auto user_name = logos::hermes::StringView(
                    nm_av, user_holder_pkg).view();
                auto sv_e = doc.make_string( std::string_view(user_name));
                if (sv_e) {
                    auto sv_off = static_cast<uint32_t>(
                        sv_e->offset().value());
                    (void)root_ptr().put(la::NAME.code,
                        AnyVal::from_offset(HermesAccess::base(doc), arena_offset_t(sv_off))
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
                            reinterpret_cast<uint8_t*>(cp_e.get()) - HermesAccess::base(doc));
                        (void)pp_arr_ptr().push_back(
                            AnyVal::from_offset(HermesAccess::base(doc), arena_offset_t(cp_off))
                            );
                    }
                    (void)root_ptr().put(la::mod::PATH_PARTS.code,
                        AnyVal::from_offset(HermesAccess::base(doc), arena_offset_t(pp_off))
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
                auto* user_base = user_holder->base();
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
                        AnyVal::from_offset(HermesAccess::base(doc), arena_offset_t(synth_uses_off))
                        );
                } else {
                    synth_uses_off = static_cast<uint32_t>(
                        synth_uses_av.to_offset(HermesAccess::base(doc)).value());
                }
                // Walk user's USE entries; for each, build the dotted
                // package name and dedup-append a fresh USE node into
                // the synth USES array. NAME-only form (build_import_scope
                // is happy with that).
                for (uint64_t i = 0; i < un; ++i) {
                    AnyVal eav = user_uses.get(i);
                    if (!eav.is_pointer()) continue;
                    auto unode = as_tinymap(eav, user_holder);
                    std::string dotted;
                    if (unode.has_key(la::NAME.code)) {
                        AnyVal nm_av = unode.get(la::NAME.code);
                        if (!nm_av.is_null() && nm_av.is_pointer()) {
                            dotted = std::string(logos::hermes::StringView(
                                nm_av, user_holder).view());
                        }
                    }
                    if (unode.has_key(la::mod::PATH_PARTS.code)) {
                        AnyVal pp_av = unode.get(la::mod::PATH_PARTS.code);
                        if (!pp_av.is_null() && pp_av.is_pointer()) {
                            auto parts = as_array(pp_av, user_holder);
                            for (uint64_t pi = 0; pi < parts.size(); ++pi) {
                                AnyVal pav = parts.get(pi);
                                if (!pav.is_pointer()) continue;
                                auto part = as_tinymap(pav, user_holder);
                                if (!part.has_key(la::NAME.code)) continue;
                                AnyVal nv = part.get(la::NAME.code);
                                if (nv.is_null() || !nv.is_pointer()) continue;
                                if (!dotted.empty()) dotted += '.';
                                dotted += std::string(logos::hermes::StringView(
                                    nv, user_holder).view());
                            }
                        }
                    }
                    if (dotted.empty()) continue;
                    // Dedup: skip if already present in synth USES (synth
                    // already has handler's imports + own self-use baked in).
                    bool already = false;
                    auto synth_uses_ptr = ArrayView(arena_offset_t(synth_uses_off), doc.holder());
                    for (uint64_t si = 0; si < synth_uses_ptr.size(); ++si) {
                        AnyVal sav = synth_uses_ptr.get(si);
                        if (!sav.is_pointer()) continue;
                        auto snode = as_tinymap(sav, user_holder);
                        if (!snode.has_key(la::NAME.code)) continue;
                        AnyVal sn_av = snode.get(la::NAME.code);
                        if (sn_av.is_null() || !sn_av.is_pointer()) continue;
                        std::string s_existing(logos::hermes::StringView(
                            sn_av, doc.holder()).view());
                        if (s_existing == dotted) { already = true; break; }
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
                        AnyVal::from_offset(HermesAccess::base(doc), arena_offset_t(pname_off))
                        );
                    auto sa = ArrayView(arena_offset_t(synth_uses_off), doc.holder());
                    (void)sa.push_back(
                        AnyVal::from_offset(HermesAccess::base(doc), arena_offset_t(utom_off))
                        );
                }
            }
        }
    }

    g_asts->push_back(std::move(doc));
    g_filenames->emplace_back("<metaprog-blob-subst>");
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
// which the splice path then decodes via lower_hermes_blob.
//
// Returns ptr past the 8-byte `[u64 size]` prefix so the result is
// directly usable as an `ExprBlob.ptr` / `HermesStatic.ptr`. Aborts on
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
// prefix into the Hermes bytes. We snapshot bytes (size = *(p-8)) so the
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
    auto depth_of = [&](uint64_t i) -> uint8_t {
        return depths ? depths[i] : 1;
    };

    uint64_t hdr_bytes = 8 + n * sizeof(CursorHdr);
    // First pass: per-cursor OUTER count, total pods, total inner_counts
    // entries (= Σ outer counts of depth-2 cursors), and total ident bytes.
    std::vector<uint64_t> outer_counts(n, 0);
    uint64_t total_pods = 0, total_str = 0, total_ic = 0;
    for (uint64_t i = 0; i < n; ++i) {
        if (depth_of(i) >= 2) {
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
        if (depth_of(i) >= 2) {
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

// hermes2 metacall freeze: the Hermes-returning thunk passes the Rc<Hermes>'s
// root as a VALUE-FORM HAny word; deep-copy the reachable tree into a compact
// single-segment blob and return a malloc'd [u64 size][bytes] buffer (ptr past
// the prefix — the same wire shape as HermesStatic; driver reads *(ptr-8)).
extern "C" const uint8_t* logos_metacall_freeze2(uint64_t root_word) {
    using logos::hermes::AnyVal;
    // The Logos side passes the VALUE-form word (Pod tagged / ABSOLUTE pointer).
    // C++ AnyVal is the AT-REST form (self-relative Ref) — from_raw(absolute)
    // would resolve relative to the stack slot. Re-anchor Refs via set_ref.
    AnyVal root;
    if (root_word & 1) {
        root = AnyVal::from_raw(static_cast<int64_t>(root_word));   // Pod: verbatim
    } else if (root_word != 0) {
        root.set_ref(reinterpret_cast<const void*>(root_word));     // Ref: re-anchor
    }
    auto packed_r = logos::hermes::compactify_root(root);
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
// `quote_expr!`. lower_quote_expr packs a wrapper Hermes doc whose
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
//   5. malloc'd HermesStatic-shaped buffer: [u64 size][bytes], return
//      ptr+8 to match ExprBlob ABI.
extern "C" const uint8_t* logos_quote_expr_subst(
        const uint8_t* tpl, uint64_t tpl_size,
        const void* idents_ptr, uint64_t idents_count) {
    namespace la = logos::compiler::ast;
    using logos::hermes::AnyVal;
    using logos::hermes::ArenaString;
    using logos::hermes::HermesAccess;
    using logos::hermes::ObjectArray;
    using logos::hermes::TinyObjectMap;
    using logos::hermes::TinyMapView;
    using logos::hermes::ArrayView;
    using logos::hermes::MapView;
    using logos::hermes::as_tinymap;
    using logos::hermes::as_array;
    using logos::hermes::arena_offset_t;
    using logos::hermes::clone;
    using logos::hermes::make_doc;

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

    auto src_e = logos::hermes::from_bytes_copy(tpl, tpl_size);
    if (!src_e) {
        std::fprintf(stderr,
            "logos_quote_expr_subst: from_bytes_copy failed\n");
        return nullptr;
    }
    auto src_doc = std::move(src_e).get();
    const uint8_t* src_base = HermesAccess::base(src_doc);

    auto wrapper_off = HermesAccess::root_offset(src_doc);
    auto wrapper = logos::hermes::TinyMapView(arena_offset_t(wrapper_off.value()), src_doc.holder());
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
                          logos::hermes::ArenaMode::GrowableSingleChunk);
    if (!dst_e) return nullptr;
    auto dst_doc = std::move(dst_e).get();
    auto& dst_arena = HermesAccess::arena(dst_doc);

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
        auto tag0 = logos::hermes::TypeTag::read_before(src_base + off);
        uint64_t tc0 = tag0.type_code();
        if (tc0 == 100) {
            auto arr = logos::hermes::ArrayView(arena_offset_t(off), src_doc.holder());
            uint64_t n = arr.size();
            for (uint64_t i = 0; i < n; ++i) {
                AnyVal el = arr.get(i);
                if (el.is_null() || !el.is_pointer()) continue;
                uint64_t r = find_cursor_count(
                    static_cast<uint32_t>(el.to_offset(src_base).value()));
                if (r > 0) return r;
            }
            return 0;
        }
        if (tc0 == 130) return 0;
        // Treat as TOM. Check for NAME_VAR(int idx) on this node.
        auto tt = logos::hermes::TinyMapView(arena_offset_t(off), src_doc.holder());
        if (tt.has_key(la::NAME_VAR.code)) {
            AnyVal iv = tt.get(la::NAME_VAR.code);
            if (!iv.is_null() && !iv.is_pointer()) {
                int32_t idx = iv.as_value<int32_t>();
                if (idx >= 0
                    && static_cast<uint64_t>(idx) < idents_count) {
                    auto sp = get_span(idx);
                    // kind=1 (scalar ExprBlob) — never a cursor.
                    // kind=0 (Ident scalar/cursor) and kind=2 (Vec<ExprBlob>
                    // cursor) — only contribute when count > 1.
                    if (sp.kind != 1 && sp.count > 1) return sp.count;
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
            if (r > 0) return r;
        }
        return 0;
    };

    // Step 5c Option B: deep-copy any Hermes node tree (TOM + ObjectArray
    // + ArenaString) from an arbitrary source base into dst_doc, with no
    // antiquot substitution. Used to splice the body of a captured
    // ExprBlob into the outer template at a `#name` placeholder.
    std::function<uint32_t(logos::hermes::MemHolder*, uint32_t)> copy_node_raw;
    copy_node_raw = [&](logos::hermes::MemHolder* sh, uint32_t off) -> uint32_t {
        const uint8_t* sb = sh->base();
        auto tag = logos::hermes::TypeTag::read_before(sb + off);
        uint64_t tc = tag.type_code();
        if (tc == 130) {
            auto se = dst_doc.make_string(
                logos::hermes::StringView(arena_offset_t(off), sh).view());
            if (!se) return 0;
            return static_cast<uint32_t>(
                se->offset().value());
        }
        if (tc == 100) {
            auto arr = logos::hermes::ArrayView(arena_offset_t(off), sh);
            uint64_t n = arr.size();
            auto dst_e = dst_doc.make_array( std::max<uint64_t>(4, n));
            if (!dst_e) return 0;
            uint32_t dst_off = static_cast<uint32_t>(
                dst_e->offset().value());
            auto dst_arr = [&]() {
                return logos::hermes::ArrayView(arena_offset_t(dst_off), dst_doc.holder());
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
                    AnyVal::from_offset(HermesAccess::base(dst_doc), arena_offset_t(no)));
            }
            return dst_off;
        }
        // TOM (tag 98 or untagged AST node).
        auto tom = logos::hermes::TinyMapView(arena_offset_t(off), sh);
        uint8_t cap = static_cast<uint8_t>(tom.capacity());
        if (cap < 4) cap = 4;
        auto dst_e = dst_doc.make_tiny_map_view( cap);
        if (!dst_e) return 0;
        uint32_t dst_off = static_cast<uint32_t>(
            dst_e->offset().value());
        auto dst_tom = [&]() {
            return logos::hermes::TinyMapView(arena_offset_t(dst_off), dst_doc.holder());
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
                AnyVal::from_offset(HermesAccess::base(dst_doc), arena_offset_t(no)));
        }
        return dst_off;
    };

    // Inner ExprBlob docs deserialised at splice time must outlive the
    // copy_node_raw recursion (their bytes back the src_base pointer).
    std::vector<logos::hermes::Hermes> inner_blob_docs;

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
                return logos::hermes::TinyMapView(arena_offset_t(bin_off), dst_doc.holder());
            };
            (void)bin().put(la::CODE.code,
                AnyVal::from_value<int32_t>(la::BINOP.code));
            auto op_e = dst_doc.make_string( std::string_view("&&"));
            if (!op_e) return 0;
            uint32_t op_off = static_cast<uint32_t>(
                op_e->offset().value());
            (void)bin().put(la::OP.code,
                AnyVal::from_offset(HermesAccess::base(dst_doc), arena_offset_t(op_off)));
            (void)bin().put(la::LHS.code,
                AnyVal::from_offset(HermesAccess::base(dst_doc), arena_offset_t(acc)));
            (void)bin().put(la::RHS.code,
                AnyVal::from_offset(HermesAccess::base(dst_doc), arena_offset_t(rhs)));
            bin().set_schema_type_code(
                logos::hermes::schema::ast(la::BINOP.code));
            acc = bin_off;
        }
        cursor_i = -1;
        return acc;
    };

    // Recursive copy: src_off (in src_doc) → fresh dst offset (in dst_doc),
    // applying placeholder substitution and REPEAT_GROUP expansion.
    copy_expr = [&](uint32_t src_off) -> uint32_t {
        auto src_tom = logos::hermes::TinyMapView(arena_offset_t(src_off), src_doc.holder());
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
            if (n == 0) {
                std::fprintf(stderr,
                    "logos_quote_expr_subst: REPEAT_GROUP cursor count is 0\n");
                return 0;
            }
            return expand_andand(body_off, n);
        }

        // 5c Option B: ExprBlob splice. Detect VAR_REF placeholders whose
        // span kind=1 BEFORE allocating a TOM — we replace the entire node
        // with a deep copy of the blob's root expr.
        // Slice 1.6: kind=2 is the Vec<ExprBlob> cursor flavor — slots
        // is a contiguous *const u8 array (8-byte stride), and we pick
        // the cursor_i-th element per `#(...)*` iteration.
        if (cd == la::VAR_REF.code && src_tom.has_key(la::NAME_VAR.code)) {
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
                        auto inner_e = logos::hermes::from_bytes_copy(
                            blob_data, blob_size);
                        if (!inner_e) {
                            std::fprintf(stderr,
                                "logos_quote_expr_subst: ExprBlob splice — from_bytes_copy failed\n");
                            return 0;
                        }
                        inner_blob_docs.push_back(std::move(inner_e).get());
                        auto& inner_doc = inner_blob_docs.back();
                        uint32_t inner_root = static_cast<uint32_t>(
                            HermesAccess::root_offset(inner_doc).value());
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
            return logos::hermes::TinyMapView(arena_offset_t(dst_off), dst_doc.holder());
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
                AnyVal::from_offset(HermesAccess::base(dst_doc), arena_offset_t(name_off)));
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
        //   - 28  (HermesString): deep-copy bytes via ArenaString::create
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
                // FIELD_READ stores its name in the FIELD key; everywhere else
                // (VAR_REF, FIELD_INIT, …) it lives in NAME.
                uint8_t out_key = (cd == la::FIELD_READ.code)
                    ? la::FIELD.code : la::NAME.code;
                (void)dst_tom().put(out_key,
                    AnyVal::from_offset(HermesAccess::base(dst_doc), arena_offset_t(name_off)));
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
            auto tag = logos::hermes::TypeTag::read_before(
                src_base + child_off);
            uint64_t tc = tag.type_code();

            if (tc == 130) {
                auto se = dst_doc.make_string(logos::hermes::StringView(arena_offset_t(child_off), src_doc.holder()).view());
                if (!se) return 0;
                uint32_t s_off = static_cast<uint32_t>(
                    se->offset().value());
                (void)dst_tom().put(k,
                    AnyVal::from_offset(HermesAccess::base(dst_doc), arena_offset_t(s_off)));
            } else if (tc == 100) {
                uint32_t new_off = copy_array(child_off);
                if (new_off == 0) return 0;
                (void)dst_tom().put(k,
                    AnyVal::from_offset(HermesAccess::base(dst_doc), arena_offset_t(new_off)));
            } else {
                uint32_t new_off = copy_expr(child_off);
                if (new_off == 0) return 0;
                (void)dst_tom().put(k,
                    AnyVal::from_offset(HermesAccess::base(dst_doc), arena_offset_t(new_off)));
            }
        }
        return dst_off;
    };

    // ObjectArray copy with REPEAT_GROUP splice. Elements that are
    // REPEAT_GROUP TOMs with sep=0 (`*`) or sep=1 (`,*`) expand into N
    // substituted bodies spliced inline as siblings; sep=2 (`&&*`) collapses
    // to a single BinOp tree (handled by copy_expr).
    copy_array = [&](uint32_t src_off) -> uint32_t {
        auto src_arr = logos::hermes::ArrayView(arena_offset_t(src_off), src_doc.holder());
        uint64_t n_src = src_arr.size();
        auto dst_e = dst_doc.make_array( std::max<uint64_t>(4, n_src));
        if (!dst_e) return 0;
        uint32_t dst_off = static_cast<uint32_t>(
            dst_e->offset().value());
        auto dst_arr = [&]() {
            return logos::hermes::ArrayView(arena_offset_t(dst_off), dst_doc.holder());
        };
        for (uint64_t i = 0; i < n_src; ++i) {
            AnyVal el = src_arr.get(i);
            if (!el.is_pointer()) {
                (void)dst_arr().push_back(el);
                continue;
            }
            uint32_t el_off =
                static_cast<uint32_t>(el.to_offset(src_base).value());
            auto el_tom = logos::hermes::TinyMapView(arena_offset_t(el_off), src_doc.holder());
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
                if (n == 0) {
                    std::fprintf(stderr,
                        "logos_quote_expr_subst: REPEAT_GROUP cursor count is 0\n");
                    return 0;
                }
                int64_t saved = cursor_i;
                for (uint64_t j = 0; j < n; ++j) {
                    cursor_i = static_cast<int64_t>(j);
                    uint32_t copy_off = copy_expr(body_off);
                    if (copy_off == 0) {
                        cursor_i = saved;
                        return 0;
                    }
                    (void)dst_arr().push_back(
                        AnyVal::from_offset(HermesAccess::base(dst_doc), arena_offset_t(copy_off)));
                }
                cursor_i = saved;
            } else {
                uint32_t copy_off = copy_expr(el_off);
                if (copy_off == 0) return 0;
                (void)dst_arr().push_back(
                    AnyVal::from_offset(HermesAccess::base(dst_doc), arena_offset_t(copy_off)));
            }
        }
        return dst_off;
    };

    uint32_t new_root = copy_expr(src_root_off);
    if (new_root == 0) return nullptr;
    HermesAccess::set_root_offset(dst_doc, arena_offset_t(new_root));

    auto packed_e = compactify(dst_doc);
    if (!packed_e) {
        std::fprintf(stderr,
            "logos_quote_expr_subst: clone failed\n");
        return nullptr;
    }
    auto packed = std::move(packed_e).get();
    auto& packed_arena = HermesAccess::arena(packed);
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

// Slice 7 of metaprog-quote: hand-built BIN_OP{LIT_INT(1), "+", LIT_INT(2)}
// blob fixture used to derisk position-aware HERMES_BLOB lowering before the
// real `quote_expr! { ... }` grammar lands. ABI matches HermesStatic /
// ExprBlob (returns ptr past an 8-byte size prefix). The buffer is leaked
// intentionally — single-shot per metacall, lifetime is the compile run.
extern "C" const uint8_t* logos_test_make_bin_op_blob() {
    using logos::hermes::HermesAccess;
    using logos::hermes::ArenaString;
    using logos::hermes::AnyVal;
    using logos::hermes::TinyObjectMap;
    using logos::hermes::TinyMapView;
    using logos::hermes::ArrayView;
    using logos::hermes::arena_offset_t;
    using logos::hermes::clone;
    namespace la = logos::compiler::ast;

    // GrowableSingleChunk (single segment for base(doc)+off addressing), pre-sized
    // past any realloc — lazy-zero keeps the reserve cheap.
    auto doc_e = logos::hermes::make_doc(size_t(8) * 1024 * 1024,
                                          logos::hermes::ArenaMode::GrowableSingleChunk);
    if (!doc_e) return nullptr;
    auto doc = std::move(doc_e).get();
    auto& arena = HermesAccess::arena(doc);

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
                        AnyVal::from_offset(HermesAccess::base(doc), arena_offset_t(str_off)));
        (void)tm().put(la::SRC_LINE.code,
                        AnyVal::from_value<int32_t>(1));
        tm().set_schema_type_code(
            logos::hermes::schema::ast(la::LIT_INT.code));
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
                      AnyVal::from_offset(HermesAccess::base(doc), arena_offset_t(op_off)));
    (void)root().put(la::LHS.code,
                      AnyVal::from_offset(HermesAccess::base(doc), arena_offset_t(lhs_off)));
    (void)root().put(la::RHS.code,
                      AnyVal::from_offset(HermesAccess::base(doc), arena_offset_t(rhs_off)));
    (void)root().put(la::SRC_LINE.code,
                      AnyVal::from_value<int32_t>(1));
    root().set_schema_type_code(
        logos::hermes::schema::ast(la::BINOP.code));

    HermesAccess::set_root_offset(doc, arena_offset_t(root_off));

    auto packed_e = compactify(doc);
    if (!packed_e) return nullptr;
    auto packed = std::move(packed_e).get();
    auto& parena = HermesAccess::arena(packed);
    const uint8_t* data = parena.head().data();
    uint64_t used = parena.total_used();

    auto* buf = static_cast<uint8_t*>(std::malloc(8 + used));
    if (!buf) return nullptr;
    std::memcpy(buf, &used, 8);
    std::memcpy(buf + 8, data, used);
    return buf + 8;
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
constexpr int EXIT_USER_ERROR = 1;
constexpr int EXIT_USAGE      = 2;
constexpr int EXIT_CODEGEN    = 3;
constexpr int EXIT_LINK_IO    = 4;
constexpr int EXIT_ICE        = 5;

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
    std::vector<hermes::Hermes>& asts,
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
        std::vector<hermes::Hermes>**       a;
        std::vector<std::string>**          fn;
        std::vector<bool>**                 fb;
        size_t*                             uri;
        std::vector<std::string>**          md;
        std::vector<std::optional<EmitProvenance>>** ap;
        bool**                              ae;
        std::set<std::string>*              p_es;
        std::vector<hermes::Hermes>*        p_a;
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
    // metaprog handler running mid-sema_lower — which holds a `Hermes&` into `asts`.
    // A vector realloc there moves that element (Hermes move nulls the source), so the
    // in-flight reference would see a moved-from doc (holder_=header_=0). Reserve up
    // front (no reference held yet) so emit-driven growth never reallocs. (Hermes1's
    // copyable handle masked this; hermes2's move-on-realloc exposes it.)
    asts.reserve(asts.size() + 65536);
    filenames.reserve(filenames.size() + 65536);
    // Provenance vector: caller-provided when --dump-metaprog is on
    // (so they can read it post-dispatch); otherwise local & discarded.
    std::vector<std::optional<EmitProvenance>> local_provenance;
    auto* prov = opts.provenance_out ? opts.provenance_out : &local_provenance;
    if (!opts.dump_dir.empty()) g_ast_provenance = prov;

    auto report = [&](const char* label) {
        if (!opts.trace) return;
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
    for (int iter = 0; ; ++iter) {
        auto _t = std::chrono::steady_clock::now();
        auto opts_iter = meta_opts;
        // M6.1: delta only when we have a cache to provide skipped asts'
        // state. emit_module's dispatch runs without a cache — skipping
        // asts there would lose their symbol-table + LIR contributions
        // (no install_snapshot + no bundle splice to compensate).
        if (opts.sema_cache) {
            opts_iter.delta_start_idx = next_delta_start;
            next_delta_start = asts.size();
        }
        // Phase 6: metaprog dispatch loop doesn't currently thread is_lazy
        // through (no use case yet — metaprog handlers + their callees stay
        // eager regardless). Pass {} to keep the existing behaviour.
        prog = sema_lower(asts, filenames, from_binary, opts_iter, {},
                          opts.module_ids ? *opts.module_ids : std::vector<std::string>{});
        stat_step(_t, "sema_lower", iter);
        prog.print_diags(stderr);
        if (!prog.ok()) return 1;
        report(iter == 0 ? "sema+lower" : "sema+lower (re-run)");

        bool has_pending_item_mc = false;
        {
            using RT = lir::MetacallRetTag;
            for (const auto& s : prog.metacall_sites) {
                if (s.ret_tag() == RT::ItemBlob) { has_pending_item_mc = true; break; }
            }
        }
        if (prog.metaprog_targets.empty() && !has_pending_item_mc) break;

        if (opts.trace) {
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
        auto saved_targets  = std::move(prog.metaprog_targets);
        auto saved_handlers = std::move(prog.metaprog_handlers);
        auto meta_prog      = std::move(prog);
        stat_step(_t2, "meta_sema_lower", iter);
        if (!meta_prog.ok()) { meta_prog.print_diags(stderr); return 1; }

        std::vector<lir_view::MetacallSiteView> meta_item_sites;
        {
            using RT = lir::MetacallRetTag;
            for (const auto& s : meta_prog.metacall_sites) {
                if (s.ret_tag() == RT::ItemBlob) meta_item_sites.push_back(s);
            }
        }
        if (!meta_item_sites.empty()) {
            bool tmp_emitted = false;
            bool* prev_any = g_any_emitted;
            g_any_emitted = &tmp_emitted;
            for (const auto& s : meta_item_sites) {
                if (!s.thunk_source().empty())
                    logos_emit_source(std::string(s.thunk_source()).c_str());
            }
            g_any_emitted = prev_any;
            auto resema_opts = meta_opts;
            for (const auto& s : meta_item_sites) {
                if (!s.callee_name().empty())
                    resema_opts.metaprog_keep_fns.push_back(std::string(s.callee_name()));
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
        }
        meta_prog.functions.erase(
            std::remove_if(meta_prog.functions.begin(), meta_prog.functions.end(),
                [](const auto& f) { return f.is_metaprog_stub(); }),
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
            auto bind_sym = [&](const char* name, void* fn) -> bool {
                if (m6_meta_jit->define_symbol(name, fn)) return true;
                std::fprintf(stderr, "logosc: bind %s: %s\n",
                             name, m6_meta_jit->error_str().c_str());
                return false;
            };
            if (!bind_sym("logos_emit_source",                reinterpret_cast<void*>(&logos_emit_source))) return 1;
            if (!bind_sym("logos_emit_item_blob",             reinterpret_cast<void*>(&logos_emit_item_blob))) return 1;
            if (!bind_sym("logos_emit_item_blob_subst",       reinterpret_cast<void*>(&logos_emit_item_blob_subst))) return 1;
            if (!bind_sym("logos_qib_pack_idents",            reinterpret_cast<void*>(&logos_qib_pack_idents))) return 1;
            if (!bind_sym("logos_qib_free_idents",            reinterpret_cast<void*>(&logos_qib_free_idents))) return 1;
            if (!bind_sym("logos_qib_pack_blobs",             reinterpret_cast<void*>(&logos_qib_pack_blobs))) return 1;
            if (!bind_sym("logos_qib_free_blobs",             reinterpret_cast<void*>(&logos_qib_free_blobs))) return 1;
            if (!bind_sym("logos_qib_pack_cursors",           reinterpret_cast<void*>(&logos_qib_pack_cursors))) return 1;
            if (!bind_sym("logos_qib_free_cursors",           reinterpret_cast<void*>(&logos_qib_free_cursors))) return 1;
            if (!bind_sym("logos_metaprog_gensym",            reinterpret_cast<void*>(&logos_metaprog_gensym))) return 1;
            if (!bind_sym("logos_metacall_freeze2",           reinterpret_cast<void*>(&logos_metacall_freeze2))) return 1;
            if (!bind_sym("logos_metaprog_test_module_blob",  reinterpret_cast<void*>(&logos_metaprog_test_module_blob))) return 1;
            if (!bind_sym("logos_test_make_bin_op_blob",      reinterpret_cast<void*>(&logos_test_make_bin_op_blob))) return 1;
            if (!bind_sym("logos_quote_expr_subst",           reinterpret_cast<void*>(&logos_quote_expr_subst))) return 1;
            if (!bind_sym("logos_get_module_ast",             reinterpret_cast<void*>(&logos_get_module_ast))) return 1;
            if (!bind_sym("logos_get_module_ast_oview",       reinterpret_cast<void*>(&logos_get_module_ast_oview))) return 1;
            if (!bind_sym("logos_holder_release",             reinterpret_cast<void*>(&logos_holder_release))) return 1;
            if (!bind_sym("logos_metaprog_error",             reinterpret_cast<void*>(&logos_metaprog_error))) return 1;
            if (!bind_sym("logos_metaprog_error_at",          reinterpret_cast<void*>(&logos_metaprog_error_at))) return 1;
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
            g_user_root_idx = tgt.ast_idx();
            bool any_handler = false;
            std::string tgt_trigger(tgt.trigger());
            for (const auto& mh : saved_handlers) {
                if (mh.trigger() != tgt_trigger) continue;
                any_handler = true;
                std::string mh_hook_fn(mh.hook_fn());
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
                    if (tgt.ast_idx() < asts.size()) {
                        auto* h    = asts[tgt.ast_idx()].holder();
                        auto tom   = hermes::TinyMapView(
                                        hermes::arena_offset_t(tgt.item_offset()), h);
                        auto av = tom.get(ast::SRC_LINE.code);
                        if (!av.is_null() && av.is_value())
                            line = static_cast<int>(av.as_value<uint32_t>());
                        auto nm_av = tom.get(ast::NAME.code);
                        if (!nm_av.is_null()) {
                            auto sv = hermes::StringView(
                                nm_av, h).view();
                            target_name = std::string(sv);
                        }
                    }
                    g_current_emit_ctx = EmitProvenance{
                        tgt.ast_idx() < filenames.size() ? filenames[tgt.ast_idx()] : std::string{},
                        line, mh_hook_fn, tgt_trigger, target_name, iter,
                    };
                    g_current_emit_ctx_valid = true;
                }
                reinterpret_cast<void (*)(uint32_t)>(sym)(tgt.item_offset());
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

        for (const auto& site : meta_item_sites) {
            if (site.thunk_source().empty()) continue;
            if (site.ast_idx() >= asts.size()) continue;
            auto* sym = meta_jit->lookup(std::string(site.thunk_name()));
            if (!sym) continue;
            reinterpret_cast<void (*)()>(sym)();
            auto& doc = asts[site.ast_idx()];
            auto* h    = doc.holder();
            auto tom  = logos::hermes::TinyMapView(logos::hermes::arena_offset_t(site.expr_offset()), h);
            if (auto r = tom.put(
                    ast::CODE.code,
                    hermes::AnyVal::from_value<int32_t>(
                        ast::METACALL_ITEM_DONE.code)
                    ); !r) {
                std::fprintf(stderr,
                    "logosc: metacall item-splice (loop): CODE put failed\n");
                return 1;
            }
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
    records.insert("schema\thermes0_format\t3");
    records.insert("schema\tlir_arena_root\t"
        + std::to_string(logos::hermes::lir_arena_root::CURRENT_VERSION));

    // cat 4: layout of the Hermes types the compiler bakes into binary artifacts
    // (the .hermes0 / LIR-blob / arena format). A size/alignment change to any of
    // these breaks every previously-written blob — record sizeof+alignof so the
    // analyzer flags it. logosc links Hermes, so it emits these directly (no
    // separate offsetof tool). "everything from Hermes the compiler uses" = the
    // value encodings, headers, in-arena container headers, and table entries that
    // define the on-disk format.
    {
#define LOGOS_ABI_TYPE(T) records.insert("type\t" #T "\tsize=" \
        + std::to_string(sizeof(T)) + " align=" + std::to_string(alignof(T)))
        LOGOS_ABI_TYPE(logos::hermes::AnyVal);          // the 8-byte tagged value word
        LOGOS_ABI_TYPE(logos::hermes::ExternalRef);     // decoded cross-arena (arena_id, obj_id)
        LOGOS_ABI_TYPE(logos::hermes::TypeTag);         // per-object in-arena tag
        LOGOS_ABI_TYPE(logos::hermes::DocumentHeader);  // blob root header
        LOGOS_ABI_TYPE(logos::hermes::Chunk);           // arena chunk header
        LOGOS_ABI_TYPE(logos::hermes::ArenaString);     // in-arena string header
        LOGOS_ABI_TYPE(logos::hermes::ObjectArray);     // in-arena array header
        LOGOS_ABI_TYPE(logos::hermes::ObjectMap);       // in-arena map header
        LOGOS_ABI_TYPE(logos::hermes::TinyObjectMap);   // in-arena tiny-map (schema objects)
        LOGOS_ABI_TYPE(logos::hermes::MapEntry);        // map slot layout
        LOGOS_ABI_TYPE(logos::hermes::ImportEntry);     // .imp table entry
#undef LOGOS_ABI_TYPE
    }

    // cat 1: stdlib exported symbols. nm -p (no sort — we sort canonically via
    // the set). The mangled name encodes the signature, so a removal/signature
    // change surfaces as a dropped line.
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
            if (!sv.empty() && sv.front() != '/' && sv.front() != '.')
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
    // Parse a spec into (category,key) -> detail. Records are "<cat>\t<key>[\t<detail>]".
    auto load = [](const std::string& p,
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
            out[{cat, key}] = det;
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

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: logosc <input.logos> [-o output.o] [-O0|-O1|-O2|-O3] [--emit-mlir] [--emit-llvm] [--diag-format=text|json] [--stats]\n");
        return EXIT_USAGE;
    }

    // Initialize Hermes TypeOps registry; the @-literal builder uses
    // clone() which dispatches per-type via this registry.
    logos::hermes::hermes_init();

    const char* input_path = nullptr;
    const char* output_path = "output.o";
    bool emit_mlir = false;
    bool emit_llvm = false;
    bool jit_run   = false;                      // --jit: compile and run main() in-process
    bool expand_only = false;                    // --expand: run metaprog dispatch over input + render result back to Logos source (no codegen). Avoids stdlib build's circular-dep when derives reference each other (debt #22 alt B).
    bool test_mode   = false;                    // --test: build a test binary (synthesise main() that runs every `#[test]` fn under panic-recovery and prints a Rust-style summary).
    bool stats_flag  = false;                    // --stats: print per-phase compile-time summary at end (also turns on inline phase trace).
    const char* emit_module_manifest = nullptr;  // --emit-module <manifest>
    bool        emit_abi_flag = false;            // --emit-abi: dump ABI surface spec
    bool        print_prefix  = false;            // --print-prefix:  this version's tree root
    bool        print_lib_dir = false;            // --print-lib-dir: this version's stdlib dir
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
        else if (arg == "--jit") { jit_run = true; }
        else if (arg == "--expand") { expand_only = true; }
        else if (arg == "--emit-module" && i + 1 < argc) { emit_module_manifest = argv[++i]; }
        else if (arg == "--emit-abi") { emit_abi_flag = true; }
        else if (arg == "--print-prefix")  { print_prefix  = true; }
        else if (arg == "--print-lib-dir") { print_lib_dir = true; }
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
        mopts.only_file = only_file;
        mopts.extra_lib_files = explicit_lib_files;
        return logos::compiler::emit_module(*manifest, output_path, mopts) ? 0 : 1;
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
    std::vector<logos::hermes::Hermes> asts;
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

    // M3 step 3: merge .hermes0 v3 exports trailers from every archive on
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
                if (auto dd = sv.find(".."); dd != std::string_view::npos)
                    binary_symbols.emplace(sv.substr(dd + 2));
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
        using logos::hermes::AnyVal;
        using logos::hermes::TinyMapView;
        using logos::hermes::ArrayView;
        using logos::hermes::StringView;

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

    {
        logos::compiler::MetaprogDispatchOpts mopts;
        mopts.trace          = trace;
        mopts.dump_dir       = dump_metaprog_dir;
        mopts.dump_filter    = dump_metaprog_filter;
        mopts.archive_paths  = archive_paths;
        mopts.provenance_out = &ast_provenance;
        mopts.cfg_flags      = cfg_flags;  // Phase 2-4
        mopts.binary_symbols = binary_symbols;
        mopts.stats_out      = stats_flag ? &top_stats : nullptr;
        mopts.sema_cache     = &sema_cache;
        mopts.implicit_prelude = implicit_prelude_pkg;
        mopts.module_ids     = &module_ids;  // module system: parallel to asts; grows with it
        mopts.self_module_id = "";           // a plain user program is in the global module (no id)
        mopts.module_name_to_id = module_name_to_id;  // §B-coex: `use … from` in discovery
        if (logos::compiler::run_metaprog_dispatch(
                asts, filenames, from_binary, pre_dispatch_entry_idx, mopts) != 0)
            return 1;
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
            auto root_off = logos::hermes::HermesAccess::root_offset(asts[entry_idx]);
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
            if (fn != "<metaprog-blob-subst>" && fn != "<metaprog>") continue;
            auto* h = asts[i].holder();
            auto root_off = logos::hermes::HermesAccess::root_offset(asts[i]);
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
    default_opts.implicit_prelude = implicit_prelude_pkg;  // default-on prelude
    default_opts.module_name_to_id = module_name_to_id;    // §3: resolve `use … from <name>`
    prog = logos::compiler::sema_lower(asts, filenames, from_binary, default_opts, is_lazy, module_ids);
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
                if (logos_emit_source(std::string(site.thunk_source()).c_str()))
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
                // expression metacall (`metacall build_cfg()` → HermesStatic,
                // …) instead JIT-executes arbitrary USER runtime code whose
                // dynamic-dispatch deps (vtables, Hermes TypeCode tag tables)
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

            // Slice 7 of metaprog-quote derisk: bind the hand-built BIN_OP
            // blob fixture so `metacall_expr_blob.logos` can resolve the
            // extern fn from the metacall JIT (separate from meta_jit).
            if (!mc_jit->define_symbol("logos_test_make_bin_op_blob",
                    reinterpret_cast<void*>(&logos_test_make_bin_op_blob))) {
                std::fprintf(stderr, "logosc: bind logos_test_make_bin_op_blob (mc_jit): %s\n",
                             mc_jit->error_str().c_str());
                return 1;
            }
            if (!mc_jit->define_symbol("logos_quote_expr_subst",
                    reinterpret_cast<void*>(&logos_quote_expr_subst))) {
                std::fprintf(stderr, "logosc: bind logos_quote_expr_subst (mc_jit): %s\n",
                             mc_jit->error_str().c_str());
                return 1;
            }
            // MC1.2: item-position metacall thunks call this from inside
            // their bodies — bind on the metacall JIT alongside meta_jit.
            if (!mc_jit->define_symbol("logos_emit_item_blob_subst",
                    reinterpret_cast<void*>(&logos_emit_item_blob_subst))) {
                std::fprintf(stderr, "logosc: bind logos_emit_item_blob_subst (mc_jit): %s\n",
                             mc_jit->error_str().c_str());
                return 1;
            }
            if (!mc_jit->define_symbol("logos_qib_pack_idents",
                    reinterpret_cast<void*>(&logos_qib_pack_idents))) {
                std::fprintf(stderr, "logosc: bind logos_qib_pack_idents (mc_jit): %s\n",
                             mc_jit->error_str().c_str());
                return 1;
            }
            if (!mc_jit->define_symbol("logos_qib_free_idents",
                    reinterpret_cast<void*>(&logos_qib_free_idents))) {
                std::fprintf(stderr, "logosc: bind logos_qib_free_idents (mc_jit): %s\n",
                             mc_jit->error_str().c_str());
                return 1;
            }
            if (!mc_jit->define_symbol("logos_qib_pack_blobs",
                    reinterpret_cast<void*>(&logos_qib_pack_blobs))) {
                std::fprintf(stderr, "logosc: bind logos_qib_pack_blobs (mc_jit): %s\n",
                             mc_jit->error_str().c_str());
                return 1;
            }
            if (!mc_jit->define_symbol("logos_qib_free_blobs",
                    reinterpret_cast<void*>(&logos_qib_free_blobs))) {
                std::fprintf(stderr, "logosc: bind logos_qib_free_blobs (mc_jit): %s\n",
                             mc_jit->error_str().c_str());
                return 1;
            }
            if (!mc_jit->define_symbol("logos_qib_pack_cursors",
                    reinterpret_cast<void*>(&logos_qib_pack_cursors))) {
                std::fprintf(stderr, "logosc: bind logos_qib_pack_cursors (mc_jit): %s\n",
                             mc_jit->error_str().c_str());
                return 1;
            }
            if (!mc_jit->define_symbol("logos_qib_free_cursors",
                    reinterpret_cast<void*>(&logos_qib_free_cursors))) {
                std::fprintf(stderr, "logosc: bind logos_qib_free_cursors (mc_jit): %s\n",
                             mc_jit->error_str().c_str());
                return 1;
            }
            if (!mc_jit->define_symbol("logos_metaprog_gensym",
                    reinterpret_cast<void*>(&logos_metaprog_gensym))) {
                std::fprintf(stderr, "logosc: bind logos_metaprog_gensym (mc_jit): %s\n",
                             mc_jit->error_str().c_str());
                return 1;
            }
            if (!mc_jit->define_symbol("logos_metacall_freeze2",
                    reinterpret_cast<void*>(&logos_metacall_freeze2))) {
                std::fprintf(stderr, "logosc: bind logos_metacall_freeze2 (mc_jit): %s\n",
                             mc_jit->error_str().c_str());
                return 1;
            }
            if (!mc_jit->define_symbol("logos_macro_arg",
                    reinterpret_cast<void*>(&logos_macro_arg))) {
                std::fprintf(stderr, "logosc: bind logos_macro_arg (mc_jit): %s\n",
                             mc_jit->error_str().c_str());
                return 1;
            }

            // Function-style macros (slice 1.3b): publish the per-site
            // arg-blob table so `logos_macro_arg(site, idx)` can resolve
            // bytes for each fn-macro thunk we are about to invoke.
            // Cleared right after the loop — the bytes live in the
            // snapshot we took before move-constructing mc_prog.
            g_macro_args = &saved_macro_args;
            struct MacroArgsGuard {
                ~MacroArgsGuard() { g_macro_args = nullptr; }
            } macro_args_guard;

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
                            auto tom  = logos::hermes::TinyMapView(logos::hermes::arena_offset_t(site.expr_offset()), h);
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
                    auto tom = logos::hermes::TinyMapView(logos::hermes::arena_offset_t(site.expr_offset()), h);
                    // Determine the DONE marker from the current CODE
                    // — metacall_item and fn_macro_call_item share the
                    // ItemBlob splice path but have distinct grammar
                    // node codes and matching DONE markers.
                    int32_t done_code = logos::compiler::ast::METACALL_ITEM_DONE.code;
                    {
                        auto cav = tom.get(logos::compiler::ast::CODE.code);
                        if (!cav.is_null() && cav.is_value()) {
                            int32_t cur = cav.as_value<int32_t>();
                            if (cur == logos::compiler::ast::FN_MACRO_CALL_ITEM.code)
                                done_code = logos::compiler::ast::FN_MACRO_CALL_ITEM_DONE.code;
                        }
                    }
                    if (auto r = tom.put(
                            logos::compiler::ast::CODE.code,
                            logos::hermes::AnyVal::from_value<int32_t>(done_code)
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
                std::string blob_bytes;  // for HermesStatic ret
                bool is_float = false, is_bool = false, is_str = false, is_unsigned = false;
                bool is_hermes_blob = false;
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
                case RT::HermesStatic:
                case RT::ExprBlob: {
                    // HermesStatic = { ptr: *const u8 }, DataPlain ≤ 16B, returned in rax.
                    // ExprBlob: identical ABI; nominal-only marker for AST-fragment payload.
                    // Layout in meta-jit rodata: [u64 size_le][bytes]; ptr points past the prefix.
                    auto blob_ptr = reinterpret_cast<const uint8_t* (*)()>(sym)();
                    if (!blob_ptr) {
                        std::fprintf(stderr, "logosc: metacall HermesStatic/ExprBlob thunk returned null\n");
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
                case RT::ItemBlob:
                    // Handled above via early-continue; unreachable here.
                    std::fprintf(stderr, "logosc: internal: ItemBlob fell through\n");
                    return 1;
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

                auto& doc = asts[site.ast_idx()];
                auto* h = doc.holder();
                auto tom = logos::hermes::TinyMapView(logos::hermes::arena_offset_t(site.expr_offset()), h);

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
                    tom = logos::hermes::TinyMapView(logos::hermes::arena_offset_t(site.expr_offset()), h);
                }

                if (auto r = tom.put(logos::compiler::ast::CODE.code,
                                      logos::hermes::AnyVal::from_value<int32_t>(new_code)
                                      ); !r) {
                    std::fprintf(stderr, "logosc: metacall splice: CODE put failed\n");
                    return 1;
                }
                tom = logos::hermes::TinyMapView(logos::hermes::arena_offset_t(site.expr_offset()), h);
                tom.set_schema_type_code(
                    logos::hermes::schema::ast(static_cast<int32_t>(new_code)));
                if (auto r = tom.put(logos::compiler::ast::VALUE.code, value_av); !r) {
                    std::fprintf(stderr, "logosc: metacall splice: VALUE put failed\n");
                    return 1;
                }
                any_spliced = true;
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
            auto root_off = logos::hermes::HermesAccess::root_offset(asts[i]);
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

    // ── Step 2c: Monomorphization (also emits L-IR Hermes mirror) ─
    {
        logos::compiler::MonoOpts mopts;
        mopts.stdlib_exports = &stdlib_exports;
        prog = logos::compiler::mono_pass(std::move(prog), std::move(mopts));
    }
    prog.print_diags(stderr);
    if (!prog.ok()) return 1;
    report("mono");

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
    // Honors --emit-mlir / --emit-llvm short-circuits internally; --jit
    // returns the module for us to JIT-run here (build_jit_from_module is
    // local to this TU).
    {
        logos::compiler::LowerEmitOpts lopts;
        lopts.opt_level         = opt_level;
        lopts.function_sections = true;
        lopts.emit_mlir         = emit_mlir;
        lopts.emit_llvm         = emit_llvm;
        lopts.dump_metaprog_dir = dump_metaprog_dir;
        std::unique_ptr<llvm::Module> jit_module;
        if (jit_run) lopts.jit_module_out = &jit_module;
        int rc = logos::compiler::lower_and_emit_object(prog, output_path, lopts);
        if (rc != 0) return rc;
        report("codegen+write");
        if (emit_mlir || emit_llvm) return 0;
        if (jit_run) {
            auto jit = build_jit_from_module(*jit_module, "logosc");
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
