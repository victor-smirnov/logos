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
#include <climits>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include "module_loader.hpp"
#include <logos/compiler/borrow_check.hpp>
#include <logos/compiler/ast.hpp>
#include <logos/compiler/lir.hpp>
#include <logos/compiler/mono.hpp>
#include <logos/hermes/schema_codes.hpp>

#include <logos/hermes/document.hpp>
#include <logos/hermes/type_tag.hpp>
#include <logos/hermes/mem_holder.hpp>
#include <logos/hermes/type_ops.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/access.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/clone.hpp>
#include <logos/hermes/view.hpp>
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
    using logos::hermes::arena_offset_t;
    using logos::hermes::copy_object_into;

    struct IdentPod { const uint8_t* ptr; uint64_t len; };
    struct BlobEntry { uint64_t offset; uint64_t size; };
    struct CursorHdr { uint64_t count; uint64_t pods_offset; };
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
        for (uint64_t j = 0; j < ch.count; ++j) {
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

    // Substitute placeholders. Root is MODULE TOM with ITEMS array.
    auto& arena = HermesAccess::arena(doc);
    auto root_off = HermesAccess::root_offset(doc);
    auto root_ptr = [&]() {
        return reinterpret_cast<TinyObjectMap*>(
            HermesAccess::base(doc) + root_off.value());
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
        auto* ctom = reinterpret_cast<TinyObjectMap*>(dbase + child_off);
        if (!ctom->has_key(la::NAME_VAR.code)) return false;
        AnyVal nv = ctom->get(la::NAME_VAR.code, dbase);
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
        auto* tom = reinterpret_cast<TinyObjectMap*>(dbase + off);
        if (tom->has_key(la::NAME_VAR.code)) {
            AnyVal nv = tom->get(la::NAME_VAR.code, dbase);
            if (nv.is_value()) {
                int32_t enc = nv.as_value<int32_t>();
                if (enc >= 0 && (enc & 0x400000) != 0) {
                    int32_t cidx = enc & ~0x400000;
                    if (static_cast<uint64_t>(cidx) < cursors_count) {
                        const auto& ch = cursor_hdrs[cidx];
                        if (iter < ch.count) {
                            const auto* pods = reinterpret_cast<const IdentPod*>(
                                cursors_base + ch.pods_offset);
                            const auto& pod = pods[iter];
                            auto str_e = ArenaString::create(arena,
                                std::string_view(
                                    reinterpret_cast<const char*>(pod.ptr),
                                    pod.len));
                            if (str_e) {
                                uint32_t name_off = static_cast<uint32_t>(
                                    reinterpret_cast<uint8_t*>(str_e.get())
                                    - HermesAccess::base(doc));
                                dbase = HermesAccess::base(doc);
                                tom = reinterpret_cast<TinyObjectMap*>(dbase + off);
                                (void)tom->put(la::NAME.code,
                                    AnyVal::from_offset(arena_offset_t(name_off)),
                                    arena);
                                dbase = HermesAccess::base(doc);
                                tom = reinterpret_cast<TinyObjectMap*>(dbase + off);
                                tom->remove(la::NAME_VAR.code, dbase);
                            }
                        }
                    }
                }
            }
        }
        // Snapshot children before recursion (puts may have rebased).
        std::vector<std::pair<bool, uint32_t>> children;
        dbase = HermesAccess::base(doc);
        tom = reinterpret_cast<TinyObjectMap*>(dbase + off);
        uint64_t bm = tom->bitmap();
        for (uint8_t key = 0; key < TinyObjectMap::MAX_KEYS; ++key) {
            if (!(bm & (1ULL << key))) continue;
            if (key == la::NAME_VAR.code) continue;
            AnyVal av = tom->get(key, dbase);
            if (av.is_null() || !av.is_pointer()) continue;
            uint32_t coff = static_cast<uint32_t>(av.to_offset().value());
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
            const auto* arr = reinterpret_cast<const ObjectArray*>(db2 + coff);
            std::vector<uint32_t> elem_offs;
            for (uint64_t i = 0; i < arr->size(); ++i) {
                AnyVal e = arr->get(i, db2);
                if (e.is_null() || !e.is_pointer()) continue;
                uint32_t eoff = static_cast<uint32_t>(e.to_offset().value());
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
        auto* tom = reinterpret_cast<TinyObjectMap*>(dbase + off);
        if (tom->has_key(la::NAME_VAR.code)) {
            AnyVal nv = tom->get(la::NAME_VAR.code, dbase);
            if (nv.is_value()) {
                int32_t enc = nv.as_value<int32_t>();
                if (enc >= 0 && (enc & 0x400000) != 0) {
                    int32_t cidx = enc & ~0x400000;
                    if (static_cast<uint64_t>(cidx) < cursors_count)
                        return cursor_hdrs[cidx].count;
                }
            }
        }
        uint64_t bm = tom->bitmap();
        for (uint8_t key = 0; key < TinyObjectMap::MAX_KEYS; ++key) {
            if (!(bm & (1ULL << key))) continue;
            if (key == la::NAME_VAR.code) continue;
            AnyVal av = tom->get(key, dbase);
            if (av.is_null() || !av.is_pointer()) continue;
            uint32_t coff = static_cast<uint32_t>(av.to_offset().value());
            const uint8_t* pointee = dbase + coff;
            lh::TypeTag tag = lh::TypeTag::read_before(pointee);
            if (tag.type_code() == lh::type_hash::TinyObjectMap) {
                uint64_t n = find_cursor_count_in_body(coff);
                if (n != static_cast<uint64_t>(-1)) return n;
            } else if (tag.type_code() == lh::type_hash::Array) {
                const auto* arr = reinterpret_cast<const ObjectArray*>(
                    HermesAccess::base(doc) + coff);
                for (uint64_t i = 0; i < arr->size(); ++i) {
                    AnyVal e = arr->get(i, HermesAccess::base(doc));
                    if (!e.is_pointer()) continue;
                    uint32_t eoff = static_cast<uint32_t>(e.to_offset().value());
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
        const auto* arr = reinterpret_cast<const ObjectArray*>(dbase + arr_off);
        bool any_repeat = false;
        uint64_t n_src = arr->size();
        for (uint64_t i = 0; i < n_src; ++i) {
            AnyVal e = arr->get(i, dbase);
            if (!e.is_pointer()) continue;
            uint32_t eoff = static_cast<uint32_t>(e.to_offset().value());
            const auto* etom = reinterpret_cast<const TinyObjectMap*>(dbase + eoff);
            int32_t cd = 0;
            if (etom->has_key(la::CODE.code)) {
                AnyVal cav = etom->get(la::CODE.code, dbase);
                if (!cav.is_null() && !cav.is_pointer())
                    cd = cav.as_value<int32_t>();
            }
            if (cd == la::REPEAT_GROUP.code) {
                int32_t sep = -1;
                if (etom->has_key(la::OP.code)) {
                    AnyVal sav = etom->get(la::OP.code, dbase);
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
            AnyVal e = arr->get(i, dbase);
            if (!e.is_pointer()) {
                src_els.push_back({false, 0, 0});
                src_nonptr.push_back(e);
                continue;
            }
            src_nonptr.push_back(AnyVal{});
            uint32_t eoff = static_cast<uint32_t>(e.to_offset().value());
            const auto* etom = reinterpret_cast<const TinyObjectMap*>(dbase + eoff);
            int32_t cd = 0;
            if (etom->has_key(la::CODE.code)) {
                AnyVal cav = etom->get(la::CODE.code, dbase);
                if (!cav.is_null() && !cav.is_pointer())
                    cd = cav.as_value<int32_t>();
            }
            if (cd == la::REPEAT_GROUP.code) {
                AnyVal bav = etom->get(la::VALUE.code, dbase);
                if (!bav.is_pointer()) {
                    subst_failed = true;
                    return 0;
                }
                src_els.push_back({true, 0,
                    bav.to_offset().value()});
            } else {
                src_els.push_back({false, eoff, 0});
            }
        }
        auto new_arr_e = ObjectArray::create(arena, std::max<uint64_t>(4, n_src));
        if (!new_arr_e) { subst_failed = true; return 0; }
        uint32_t new_arr_off = static_cast<uint32_t>(
            reinterpret_cast<uint8_t*>(new_arr_e.get()) - HermesAccess::base(doc));
        for (uint64_t i = 0; i < src_els.size(); ++i) {
            auto* na = reinterpret_cast<ObjectArray*>(
                HermesAccess::base(doc) + new_arr_off);
            if (!src_els[i].is_rep) {
                if (src_els[i].off == 0) {
                    (void)na->push_back(src_nonptr[i], arena);
                } else {
                    (void)na->push_back(
                        AnyVal::from_offset(arena_offset_t(src_els[i].off)),
                        arena);
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
                auto* na2 = reinterpret_cast<ObjectArray*>(
                    HermesAccess::base(doc) + new_arr_off);
                (void)na2->push_back(
                    AnyVal::from_offset(arena_offset_t(copy_off)), arena);
            }
        }
        return new_arr_off;
    };

    std::function<void(uint32_t)> subst_walk = [&](uint32_t off) {
        if (subst_failed) return;
        uint8_t* dbase = HermesAccess::base(doc);
        auto* tom = reinterpret_cast<TinyObjectMap*>(dbase + off);
        if (tom->has_key(la::NAME_VAR.code)) {
            AnyVal idx_av = tom->get(la::NAME_VAR.code, dbase);
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
                auto str_e = ArenaString::create(arena,
                    std::string_view(reinterpret_cast<const char*>(idp.ptr),
                                     idp.len));
                if (!str_e) {
                    std::fprintf(stderr,
                        "logos_emit_item_blob_subst: ArenaString alloc failed\n");
                    subst_failed = true; return;
                }
                uint32_t name_off = static_cast<uint32_t>(
                    reinterpret_cast<uint8_t*>(str_e.get())
                    - HermesAccess::base(doc));
                dbase = HermesAccess::base(doc);
                tom = reinterpret_cast<TinyObjectMap*>(dbase + off);
                (void)tom->put(la::NAME.code,
                    AnyVal::from_offset(arena_offset_t(name_off)), arena);
                dbase = HermesAccess::base(doc);
                tom = reinterpret_cast<TinyObjectMap*>(dbase + off);
                tom->remove(la::NAME_VAR.code, dbase);
            }
        }
        // Snapshot children before recursion (puts above may have rebased).
        // Track key for TOM children and elem-index for array children so
        // blob splice can rewrite the parent's slot.
        struct ChildRef { bool is_arr; uint8_t key; uint32_t coff; };
        std::vector<ChildRef> children;
        dbase = HermesAccess::base(doc);
        tom = reinterpret_cast<TinyObjectMap*>(dbase + off);
        uint64_t bm = tom->bitmap();
        for (uint8_t key = 0; key < TinyObjectMap::MAX_KEYS; ++key) {
            if (!(bm & (1ULL << key))) continue;
            if (key == la::NAME_VAR.code) continue;
            AnyVal av = tom->get(key, dbase);
            if (av.is_null() || !av.is_pointer()) continue;
            uint32_t coff = static_cast<uint32_t>(av.to_offset().value());
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
                        auto* t = reinterpret_cast<TinyObjectMap*>(db + off);
                        (void)t->put(key,
                            AnyVal::from_offset(arena_offset_t(new_off)),
                            arena);
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
                    auto* t = reinterpret_cast<TinyObjectMap*>(db + off);
                    (void)t->put(cref.key,
                        AnyVal::from_offset(arena_offset_t(expanded)), arena);
                    arr_off = expanded;
                }
            }
            uint8_t* dbase2 = HermesAccess::base(doc);
            const auto* arr =
                reinterpret_cast<const ObjectArray*>(dbase2 + arr_off);
            std::vector<std::pair<uint64_t, uint32_t>> elems;
            for (uint64_t i = 0; i < arr->size(); ++i) {
                AnyVal e = arr->get(i, dbase2);
                if (e.is_null() || !e.is_pointer()) continue;
                uint32_t eoff = static_cast<uint32_t>(e.to_offset().value());
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
                        auto* a = reinterpret_cast<ObjectArray*>(db + arr_off);
                        a->set(ei,
                               AnyVal::from_offset(arena_offset_t(new_off)),
                               db);
                    });
                if (!spliced) subst_walk(eoff);
            }
        }
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
                subst_walk(static_cast<uint32_t>(it_av.to_offset().value()));
                if (subst_failed) {
                    blob_seen.erase(key); return 0;
                }
            }
        }
    }

    g_asts->push_back(std::move(doc));
    g_filenames->emplace_back("<metaprog-blob-subst>");
    g_from_binary->push_back(false);
    *g_any_emitted = true;
    return 1;
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
extern "C" const uint8_t* logos_qib_pack_cursors(
        const void* const* arr, uint64_t n) {
    struct IdentPod  { const uint8_t* ptr; uint64_t len; };
    struct CursorHdr { uint64_t count;     uint64_t pods_offset; };
    struct VecPod    { const IdentPod* ptr; uint64_t len; uint64_t cap; };

    uint64_t hdr_bytes = 8 + n * sizeof(CursorHdr);
    // First pass: per-cursor count + sum of all ident bytes
    std::vector<uint64_t> counts(n, 0);
    uint64_t total_pods = 0;
    uint64_t total_str  = 0;
    for (uint64_t i = 0; i < n; ++i) {
        const auto* v = reinterpret_cast<const VecPod*>(arr[i]);
        if (!v) continue;
        counts[i]   = v->len;
        total_pods += v->len;
        for (uint64_t j = 0; j < v->len; ++j) {
            total_str += v->ptr[j].len;
        }
    }
    uint64_t total = hdr_bytes
                   + total_pods * sizeof(IdentPod)
                   + total_str;
    auto* buf = static_cast<uint8_t*>(std::malloc(total));
    if (!buf) return nullptr;
    *reinterpret_cast<uint64_t*>(buf) = n;
    auto* hdrs = reinterpret_cast<CursorHdr*>(buf + 8);
    uint64_t pod_off = hdr_bytes;
    uint64_t str_off = hdr_bytes + total_pods * sizeof(IdentPod);
    for (uint64_t i = 0; i < n; ++i) {
        hdrs[i].count       = counts[i];
        hdrs[i].pods_offset = pod_off;
        const auto* v = reinterpret_cast<const VecPod*>(arr[i]);
        if (!v) continue;
        auto* pods = reinterpret_cast<IdentPod*>(buf + pod_off);
        for (uint64_t j = 0; j < v->len; ++j) {
            pods[j].len = v->ptr[j].len;
            if (v->ptr[j].len > 0 && v->ptr[j].ptr) {
                std::memcpy(buf + str_off, v->ptr[j].ptr, v->ptr[j].len);
                pods[j].ptr = buf + str_off;
                str_off += v->ptr[j].len;
            } else {
                pods[j].ptr = nullptr;
            }
        }
        pod_off += v->len * sizeof(IdentPod);
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
    const auto* wrapper = reinterpret_cast<const TinyObjectMap*>(
        src_base + wrapper_off.value());
    if (!wrapper->has_key(la::VALUE.code)) {
        std::fprintf(stderr,
            "logos_quote_expr_subst: malformed wrapper TOM\n");
        return nullptr;
    }
    AnyVal expr_av = wrapper->get(la::VALUE.code,
                                  const_cast<uint8_t*>(src_base));
    if (!expr_av.is_pointer()) {
        std::fprintf(stderr,
            "logos_quote_expr_subst: wrapper VALUE not pointer\n");
        return nullptr;
    }
    uint32_t src_root_off =
        static_cast<uint32_t>(expr_av.to_offset().value());

    auto dst_e = make_doc(tpl_size + 256);
    if (!dst_e) return nullptr;
    auto dst_doc = std::move(dst_e).get();
    auto& dst_arena = HermesAccess::arena(dst_doc);

    // Cursor index when inside a REPEAT_GROUP iteration (-1 outside).
    int64_t cursor_i = -1;

    auto str_for_ident = [&](const IdentPod* idp) -> uint32_t {
        if (!idp || !idp->ptr || idp->len == 0) return 0;
        auto str_e = ArenaString::create(dst_arena,
            std::string_view(reinterpret_cast<const char*>(idp->ptr),
                             idp->len));
        if (!str_e) return 0;
        return static_cast<uint32_t>(
            reinterpret_cast<uint8_t*>(str_e.get())
            - HermesAccess::base(dst_doc));
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
            const auto* arr = reinterpret_cast<const ObjectArray*>(
                src_base + off);
            uint64_t n = arr->size();
            for (uint64_t i = 0; i < n; ++i) {
                AnyVal el = const_cast<ObjectArray*>(arr)->get(
                    i, const_cast<uint8_t*>(src_base));
                if (el.is_null() || !el.is_pointer()) continue;
                uint64_t r = find_cursor_count(
                    static_cast<uint32_t>(el.to_offset().value()));
                if (r > 0) return r;
            }
            return 0;
        }
        if (tc0 == 28) return 0;
        // Treat as TOM. Check for NAME_VAR(int idx) on this node.
        const auto* tt = reinterpret_cast<const TinyObjectMap*>(
            src_base + off);
        if (tt->has_key(la::NAME_VAR.code)) {
            AnyVal iv = tt->get(la::NAME_VAR.code,
                                const_cast<uint8_t*>(src_base));
            if (!iv.is_null() && !iv.is_pointer()) {
                int32_t idx = iv.as_value<int32_t>();
                if (idx >= 0
                    && static_cast<uint64_t>(idx) < idents_count) {
                    auto sp = get_span(idx);
                    if (sp.kind != 1 && sp.count > 1) return sp.count;
                }
            }
        }
        // Recurse into every TOM-typed pointer key and every array key.
        for (uint8_t k = 0; k < TinyObjectMap::MAX_KEYS; ++k) {
            if (!tt->has_key(k)) continue;
            AnyVal av = tt->get(k, const_cast<uint8_t*>(src_base));
            if (av.is_null() || !av.is_pointer()) continue;
            uint32_t coff = static_cast<uint32_t>(av.to_offset().value());
            uint64_t r = find_cursor_count(coff);
            if (r > 0) return r;
        }
        return 0;
    };

    // Step 5c Option B: deep-copy any Hermes node tree (TOM + ObjectArray
    // + ArenaString) from an arbitrary source base into dst_doc, with no
    // antiquot substitution. Used to splice the body of a captured
    // ExprBlob into the outer template at a `#name` placeholder.
    std::function<uint32_t(const uint8_t*, uint32_t)> copy_node_raw;
    copy_node_raw = [&](const uint8_t* sb, uint32_t off) -> uint32_t {
        auto tag = logos::hermes::TypeTag::read_before(sb + off);
        uint64_t tc = tag.type_code();
        if (tc == 28) {
            const auto* s = reinterpret_cast<const ArenaString*>(sb + off);
            auto se = ArenaString::create(dst_arena, s->view());
            if (!se) return 0;
            return static_cast<uint32_t>(
                reinterpret_cast<uint8_t*>(se.get())
                - HermesAccess::base(dst_doc));
        }
        if (tc == 100) {
            const auto* arr = reinterpret_cast<const ObjectArray*>(sb + off);
            uint64_t n = arr->size();
            auto dst_e = ObjectArray::create(dst_arena, std::max<uint64_t>(4, n));
            if (!dst_e) return 0;
            uint32_t dst_off = static_cast<uint32_t>(
                reinterpret_cast<uint8_t*>(dst_e.get())
                - HermesAccess::base(dst_doc));
            auto dst_arr = [&]() {
                return reinterpret_cast<ObjectArray*>(
                    HermesAccess::base(dst_doc) + dst_off);
            };
            for (uint64_t i = 0; i < n; ++i) {
                AnyVal el = const_cast<ObjectArray*>(arr)->get(
                    i, const_cast<uint8_t*>(sb));
                if (!el.is_pointer()) {
                    (void)dst_arr()->push_back(el, dst_arena);
                    continue;
                }
                uint32_t no = copy_node_raw(sb,
                    static_cast<uint32_t>(el.to_offset().value()));
                if (no == 0) return 0;
                (void)dst_arr()->push_back(
                    AnyVal::from_offset(arena_offset_t(no)),
                    dst_arena);
            }
            return dst_off;
        }
        // TOM (tag 98 or untagged AST node).
        const auto* tom = reinterpret_cast<const TinyObjectMap*>(sb + off);
        uint8_t cap = tom->capacity();
        if (cap < 4) cap = 4;
        auto dst_e = HermesAccess::raw_tiny_map(dst_doc, cap);
        if (!dst_e) return 0;
        uint32_t dst_off = static_cast<uint32_t>(
            reinterpret_cast<uint8_t*>(dst_e.get())
            - HermesAccess::base(dst_doc));
        auto dst_tom = [&]() {
            return reinterpret_cast<TinyObjectMap*>(
                HermesAccess::base(dst_doc) + dst_off);
        };
        if (tom->schema_type_code() != 0) {
            dst_tom()->set_schema_type_code(tom->schema_type_code());
        }
        for (uint8_t k = 0; k < TinyObjectMap::MAX_KEYS; ++k) {
            if (!tom->has_key(k)) continue;
            AnyVal av = tom->get(k, const_cast<uint8_t*>(sb));
            if (av.is_null() || !av.is_pointer()) {
                (void)dst_tom()->put(k, av, dst_arena);
                continue;
            }
            uint32_t no = copy_node_raw(sb,
                static_cast<uint32_t>(av.to_offset().value()));
            if (no == 0) return 0;
            (void)dst_tom()->put(k,
                AnyVal::from_offset(arena_offset_t(no)), dst_arena);
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
            auto bin_e = HermesAccess::raw_tiny_map(dst_doc, 4);
            if (!bin_e) return 0;
            uint32_t bin_off = static_cast<uint32_t>(
                reinterpret_cast<uint8_t*>(bin_e.get())
                - HermesAccess::base(dst_doc));
            auto bin = [&]() {
                return reinterpret_cast<TinyObjectMap*>(
                    HermesAccess::base(dst_doc) + bin_off);
            };
            (void)bin()->put(la::CODE.code,
                AnyVal::from_value<int32_t>(la::BINOP.code), dst_arena);
            auto op_e = ArenaString::create(dst_arena, std::string_view("&&"));
            if (!op_e) return 0;
            uint32_t op_off = static_cast<uint32_t>(
                reinterpret_cast<uint8_t*>(op_e.get())
                - HermesAccess::base(dst_doc));
            (void)bin()->put(la::OP.code,
                AnyVal::from_offset(arena_offset_t(op_off)), dst_arena);
            (void)bin()->put(la::LHS.code,
                AnyVal::from_offset(arena_offset_t(acc)), dst_arena);
            (void)bin()->put(la::RHS.code,
                AnyVal::from_offset(arena_offset_t(rhs)), dst_arena);
            bin()->set_schema_type_code(
                logos::hermes::schema::ast(la::BINOP.code));
            acc = bin_off;
        }
        cursor_i = -1;
        return acc;
    };

    // Recursive copy: src_off (in src_doc) → fresh dst offset (in dst_doc),
    // applying placeholder substitution and REPEAT_GROUP expansion.
    copy_expr = [&](uint32_t src_off) -> uint32_t {
        const auto* src_tom = reinterpret_cast<const TinyObjectMap*>(
            src_base + src_off);
        int32_t cd = 0;
        if (src_tom->has_key(la::CODE.code)) {
            AnyVal cav = src_tom->get(la::CODE.code,
                                      const_cast<uint8_t*>(src_base));
            if (!cav.is_null() && !cav.is_pointer())
                cd = cav.as_value<int32_t>();
        }

        // REPEAT_GROUP: expand here. Caller is the expr edge in parent.
        if (cd == la::REPEAT_GROUP.code) {
            int32_t sep = 0;
            if (src_tom->has_key(la::OP.code)) {
                AnyVal sav = src_tom->get(la::OP.code,
                                          const_cast<uint8_t*>(src_base));
                if (!sav.is_null() && !sav.is_pointer())
                    sep = sav.as_value<int32_t>();
            }
            if (sep != 2) {
                std::fprintf(stderr,
                    "logos_quote_expr_subst: REPEAT_GROUP sep %d not implemented\n",
                    sep);
                return 0;
            }
            AnyVal bav = src_tom->get(la::VALUE.code,
                                      const_cast<uint8_t*>(src_base));
            if (!bav.is_pointer()) return 0;
            uint32_t body_off =
                static_cast<uint32_t>(bav.to_offset().value());
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
        // with a deep copy of the blob's root expr. Cursor expansion of
        // ExprBlob (e.g. `[ExprBlob; N]`) is not yet supported; the blob
        // is always a scalar splice.
        if (cd == la::VAR_REF.code && src_tom->has_key(la::NAME_VAR.code)) {
            AnyVal iv0 = src_tom->get(la::NAME_VAR.code,
                                      const_cast<uint8_t*>(src_base));
            if (!iv0.is_null() && !iv0.is_pointer()) {
                int32_t idx0 = iv0.as_value<int32_t>();
                if (idx0 >= 0
                    && static_cast<uint64_t>(idx0) < idents_count) {
                    SpanView sp0 = get_span(idx0);
                    if (sp0.kind == 1) {
                        const uint8_t* blob_data =
                            reinterpret_cast<const uint8_t*>(sp0.slots);
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
                        const uint8_t* ib = HermesAccess::base(inner_doc);
                        uint32_t inner_root = static_cast<uint32_t>(
                            HermesAccess::root_offset(inner_doc).value());
                        return copy_node_raw(ib, inner_root);
                    }
                }
            }
        }

        // Allocate a fresh dst TOM for this node; copy keys.
        uint8_t cap = src_tom->capacity();
        if (cap < 4) cap = 4;
        auto dst_e = HermesAccess::raw_tiny_map(dst_doc, cap);
        if (!dst_e) return 0;
        uint32_t dst_off = static_cast<uint32_t>(
            reinterpret_cast<uint8_t*>(dst_e.get())
            - HermesAccess::base(dst_doc));
        auto dst_tom = [&]() {
            return reinterpret_cast<TinyObjectMap*>(
                HermesAccess::base(dst_doc) + dst_off);
        };
        if (src_tom->schema_type_code() != 0) {
            dst_tom()->set_schema_type_code(src_tom->schema_type_code());
        }

        // VAR_REF with NAME_VAR(int idx) → emit NAME(string_from_ident).
        if (cd == la::VAR_REF.code && src_tom->has_key(la::NAME_VAR.code)) {
            AnyVal iv = src_tom->get(la::NAME_VAR.code,
                                     const_cast<uint8_t*>(src_base));
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
            (void)dst_tom()->put(la::CODE.code,
                AnyVal::from_value<int32_t>(la::VAR_REF.code), dst_arena);
            (void)dst_tom()->put(la::NAME.code,
                AnyVal::from_offset(arena_offset_t(name_off)), dst_arena);
            // Copy SRC_LINE if present so error messages keep line info.
            if (src_tom->has_key(la::SRC_LINE.code)) {
                AnyVal lav = src_tom->get(la::SRC_LINE.code,
                                          const_cast<uint8_t*>(src_base));
                if (!lav.is_null() && !lav.is_pointer()) {
                    (void)dst_tom()->put(la::SRC_LINE.code, lav, dst_arena);
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
            if (!src_tom->has_key(k)) continue;
            AnyVal av = src_tom->get(k, const_cast<uint8_t*>(src_base));
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
                (void)dst_tom()->put(out_key,
                    AnyVal::from_offset(arena_offset_t(name_off)), dst_arena);
                continue;
            }
            if (av.is_null()) {
                (void)dst_tom()->put(k, av, dst_arena);
                continue;
            }
            if (!av.is_pointer()) {
                (void)dst_tom()->put(k, av, dst_arena);
                continue;
            }
            uint32_t child_off =
                static_cast<uint32_t>(av.to_offset().value());
            auto tag = logos::hermes::TypeTag::read_before(
                src_base + child_off);
            uint64_t tc = tag.type_code();

            if (tc == 28) {
                const auto* s = reinterpret_cast<const ArenaString*>(
                    src_base + child_off);
                auto se = ArenaString::create(dst_arena, s->view());
                if (!se) return 0;
                uint32_t s_off = static_cast<uint32_t>(
                    reinterpret_cast<uint8_t*>(se.get())
                    - HermesAccess::base(dst_doc));
                (void)dst_tom()->put(k,
                    AnyVal::from_offset(arena_offset_t(s_off)), dst_arena);
            } else if (tc == 100) {
                uint32_t new_off = copy_array(child_off);
                if (new_off == 0) return 0;
                (void)dst_tom()->put(k,
                    AnyVal::from_offset(arena_offset_t(new_off)), dst_arena);
            } else {
                uint32_t new_off = copy_expr(child_off);
                if (new_off == 0) return 0;
                (void)dst_tom()->put(k,
                    AnyVal::from_offset(arena_offset_t(new_off)), dst_arena);
            }
        }
        return dst_off;
    };

    // ObjectArray copy with REPEAT_GROUP splice. Elements that are
    // REPEAT_GROUP TOMs with sep=0 (`*`) or sep=1 (`,*`) expand into N
    // substituted bodies spliced inline as siblings; sep=2 (`&&*`) collapses
    // to a single BinOp tree (handled by copy_expr).
    copy_array = [&](uint32_t src_off) -> uint32_t {
        const auto* src_arr = reinterpret_cast<const ObjectArray*>(
            src_base + src_off);
        uint64_t n_src = src_arr->size();
        auto dst_e = ObjectArray::create(dst_arena, std::max<uint64_t>(4, n_src));
        if (!dst_e) return 0;
        uint32_t dst_off = static_cast<uint32_t>(
            reinterpret_cast<uint8_t*>(dst_e.get())
            - HermesAccess::base(dst_doc));
        auto dst_arr = [&]() {
            return reinterpret_cast<ObjectArray*>(
                HermesAccess::base(dst_doc) + dst_off);
        };
        for (uint64_t i = 0; i < n_src; ++i) {
            AnyVal el = const_cast<ObjectArray*>(src_arr)->get(
                i, const_cast<uint8_t*>(src_base));
            if (!el.is_pointer()) {
                (void)dst_arr()->push_back(el, dst_arena);
                continue;
            }
            uint32_t el_off =
                static_cast<uint32_t>(el.to_offset().value());
            const auto* el_tom = reinterpret_cast<const TinyObjectMap*>(
                src_base + el_off);
            int32_t el_cd = 0;
            if (el_tom->has_key(la::CODE.code)) {
                AnyVal cav = el_tom->get(la::CODE.code,
                                         const_cast<uint8_t*>(src_base));
                if (!cav.is_null() && !cav.is_pointer())
                    el_cd = cav.as_value<int32_t>();
            }
            int32_t sep = -1;
            if (el_cd == la::REPEAT_GROUP.code
                && el_tom->has_key(la::OP.code)) {
                AnyVal sav = el_tom->get(la::OP.code,
                                         const_cast<uint8_t*>(src_base));
                if (!sav.is_null() && !sav.is_pointer())
                    sep = sav.as_value<int32_t>();
            }
            if (el_cd == la::REPEAT_GROUP.code
                && (sep == 0 || sep == 1)) {
                // Splice: expand body N times, push each as sibling element.
                AnyVal bav = el_tom->get(la::VALUE.code,
                                         const_cast<uint8_t*>(src_base));
                if (!bav.is_pointer()) return 0;
                uint32_t body_off =
                    static_cast<uint32_t>(bav.to_offset().value());
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
                    (void)dst_arr()->push_back(
                        AnyVal::from_offset(arena_offset_t(copy_off)),
                        dst_arena);
                }
                cursor_i = saved;
            } else {
                uint32_t copy_off = copy_expr(el_off);
                if (copy_off == 0) return 0;
                (void)dst_arr()->push_back(
                    AnyVal::from_offset(arena_offset_t(copy_off)),
                    dst_arena);
            }
        }
        return dst_off;
    };

    uint32_t new_root = copy_expr(src_root_off);
    if (new_root == 0) return nullptr;
    HermesAccess::set_root_offset(dst_doc, arena_offset_t(new_root));

    auto packed_e = clone(dst_doc);
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
    using logos::hermes::arena_offset_t;
    using logos::hermes::clone;
    namespace la = logos::compiler::ast;

    auto doc_e = logos::hermes::make_doc(4096);
    if (!doc_e) return nullptr;
    auto doc = std::move(doc_e).get();
    auto& arena = HermesAccess::arena(doc);

    auto build_lit_int = [&](int64_t v) -> uint32_t {
        auto tm_e = HermesAccess::raw_tiny_map(doc, 4);
        if (!tm_e) return 0;
        uint32_t tm_off = uint32_t(reinterpret_cast<uint8_t*>(tm_e.get())
                                   - HermesAccess::base(doc));
        auto str_e = ArenaString::create(arena, std::to_string(v));
        if (!str_e) return 0;
        uint32_t str_off = uint32_t(reinterpret_cast<uint8_t*>(str_e.get())
                                    - HermesAccess::base(doc));
        auto tm = [&]() {
            return reinterpret_cast<TinyObjectMap*>(
                HermesAccess::base(doc) + tm_off);
        };
        (void)tm()->put(la::CODE.code,
                        AnyVal::from_value<int32_t>(la::LIT_INT.code), arena);
        (void)tm()->put(la::VALUE.code,
                        AnyVal::from_offset(arena_offset_t(str_off)), arena);
        (void)tm()->put(la::SRC_LINE.code,
                        AnyVal::from_value<int32_t>(1), arena);
        tm()->set_schema_type_code(
            logos::hermes::schema::ast(la::LIT_INT.code));
        return tm_off;
    };

    uint32_t lhs_off = build_lit_int(1);
    uint32_t rhs_off = build_lit_int(2);
    if (!lhs_off || !rhs_off) return nullptr;

    auto op_e = ArenaString::create(arena, std::string_view("+"));
    if (!op_e) return nullptr;
    uint32_t op_off = uint32_t(reinterpret_cast<uint8_t*>(op_e.get())
                               - HermesAccess::base(doc));

    auto root_e = HermesAccess::raw_tiny_map(doc, 8);
    if (!root_e) return nullptr;
    uint32_t root_off = uint32_t(reinterpret_cast<uint8_t*>(root_e.get())
                                 - HermesAccess::base(doc));
    auto root = [&]() {
        return reinterpret_cast<TinyObjectMap*>(
            HermesAccess::base(doc) + root_off);
    };
    (void)root()->put(la::CODE.code,
                      AnyVal::from_value<int32_t>(la::BINOP.code), arena);
    (void)root()->put(la::OP.code,
                      AnyVal::from_offset(arena_offset_t(op_off)), arena);
    (void)root()->put(la::LHS.code,
                      AnyVal::from_offset(arena_offset_t(lhs_off)), arena);
    (void)root()->put(la::RHS.code,
                      AnyVal::from_offset(arena_offset_t(rhs_off)), arena);
    (void)root()->put(la::SRC_LINE.code,
                      AnyVal::from_value<int32_t>(1), arena);
    root()->set_schema_type_code(
        logos::hermes::schema::ast(la::BINOP.code));

    HermesAccess::set_root_offset(doc, arena_offset_t(root_off));

    auto packed_e = clone(doc);
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
    auto resolve_system_lib_dir = []() -> std::string {
        if (const char* env = std::getenv("LOGOS_LIB_DIR")) return env;
#ifdef LOGOS_LIB_RELDIR
        char exe[PATH_MAX];
        ssize_t n = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (n > 0) {
            exe[n] = '\0';
            std::string p(exe);
            if (auto slash = p.rfind('/'); slash != std::string::npos)
                p.resize(slash);
            p += "/" LOGOS_LIB_RELDIR;
            char real[PATH_MAX];
            if (::realpath(p.c_str(), real)) return real;
        }
#endif
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

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) { output_path = argv[++i]; }
        else if (arg == "-I" && i + 1 < argc) { search_paths.push_back(argv[++i]); }
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

    if (print_system_libdir) {
        auto sys = resolve_system_lib_dir();
        std::printf("%s\n", sys.c_str());
        return 0;
    }

    // Append system module library to search_paths after CLI flags so
    // user-provided -L wins (search runs front-to-back). --no-system opts
    // out for hermetic / cross-compiling builds.
    if (!no_system) {
        auto sys = resolve_system_lib_dir();
        if (!sys.empty()) search_paths.push_back(sys);
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
    bool loader_had_error = false;
    auto modules = logos::compiler::load_modules(input_path, search_paths, &loader_had_error, explicit_lib_files);
    report("load+parse");
    if (modules.empty()) {
        std::fprintf(stderr, "logosc: no modules loaded\n");
        return 1;
    }
    if (loader_had_error) {
        // B-mv-03/04: `use <pkg>;` referencing a missing package is fatal.
        // The loader already emitted "module_loader: cannot find package 'X'".
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
    // archive_paths spans every search dir (incl. system) plus -l files so
    // the metacall ORC JIT can resolve every available symbol at compile
    // time.
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
    }
    for (const auto& f : explicit_lib_files) archive_paths.push_back(f);

    // binary_symbols: only collected from user-explicit -L / -l. mlir_gen
    // consults this set to skip body emission for fns whose pre-baked
    // implementation is already in an archive the user explicitly pulled
    // in — emitting again would multiply-define. The system stdlib is
    // intentionally OUT: a project that doesn't opt in to stdlib's
    // pre-baked bodies stays free to define `fn alloc`, `fn str_len`,
    // etc. The lazy-archive linker picks user's `.o` over the archive's
    // same-named member.
    auto collect_syms = [&](const std::string& cmd) {
        FILE* pipe = ::popen(cmd.c_str(), "r");
        if (!pipe) return;
        char line[512];
        while (std::fgets(line, sizeof(line), pipe)) {
            std::string_view sv(line);
            while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r' || sv.back() == ' '))
                sv.remove_suffix(1);
            if (!sv.empty() && sv.front() != '/')
                binary_symbols.emplace(sv);
        }
        ::pclose(pipe);
    };
    for (const auto& dir : explicit_lib_paths)
        collect_syms("nm --defined-only -j " + dir + "/*.a 2>/dev/null");
    for (const auto& f : explicit_lib_files)
        collect_syms("nm --defined-only -j " + f + " 2>/dev/null");
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

        // Stay in the loop if there are pre-sema metaprog hook targets, OR
        // any item-position metacall sites still pending (ItemBlob ret tag,
        // CODE not yet flipped to METACALL_ITEM_DONE). Expr-position metacall
        // sites inside fn bodies are NOT visible here (metaprog_mode skips
        // entry-file fn bodies); they're handled by the post-loop pass below.
        bool has_pending_item_mc = false;
        {
            using RT = logos::compiler::lir::LProgram::MetacallSite::RetTag;
            for (const auto& s : prog.metacall_sites) {
                if (s.ret_tag == RT::ItemBlob) { has_pending_item_mc = true; break; }
            }
        }
        if (prog.metaprog_targets.empty() && !has_pending_item_mc) break;

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
        // MC1.2: stash item-position metacall sites — mono_pass below moves
        // meta_prog and the resulting LProgram doesn't carry metacall_sites
        // through (they're sema-pass artefacts).
        std::vector<logos::compiler::lir::LProgram::MetacallSite>
            meta_item_sites;
        {
            using RT = logos::compiler::lir::LProgram::MetacallSite::RetTag;
            for (const auto& s : meta_prog.metacall_sites) {
                if (s.ret_tag == RT::ItemBlob) meta_item_sites.push_back(s);
            }
        }
        // Emit thunk sources up front so this iter's meta_jit picks them up.
        // logos_emit_source requires g_any_emitted alive — wire a temp here;
        // the loop's main `any_emitted` is set further below for hooks.
        if (!meta_item_sites.empty()) {
            bool tmp_emitted = false;
            bool* prev_any = g_any_emitted;
            g_any_emitted = &tmp_emitted;
            for (const auto& s : meta_item_sites) {
                if (!s.thunk_source.empty()) logos_emit_source(s.thunk_source.c_str());
            }
            g_any_emitted = prev_any;
            // Add callee names to metaprog_keep_fns so re-sema lowers their
            // bodies (otherwise the thunk's call into the user fn references
            // an empty stub and MLIR-gen fails).
            auto resema_opts = meta_opts;
            for (const auto& s : meta_item_sites) {
                if (!s.callee_name.empty())
                    resema_opts.metaprog_keep_fns.push_back(s.callee_name);
            }
            meta_prog = logos::compiler::sema_lower(asts, filenames, from_binary, resema_opts);
            if (!meta_prog.ok()) { meta_prog.print_diags(stderr); return 1; }
        }
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
        if (!meta_jit->define_symbol("logos_qib_pack_idents",
                                     reinterpret_cast<void*>(&logos_qib_pack_idents))) {
            std::fprintf(stderr, "logosc: bind logos_qib_pack_idents: %s\n",
                         meta_jit->error_str().c_str());
            return 1;
        }
        if (!meta_jit->define_symbol("logos_qib_free_idents",
                                     reinterpret_cast<void*>(&logos_qib_free_idents))) {
            std::fprintf(stderr, "logosc: bind logos_qib_free_idents: %s\n",
                         meta_jit->error_str().c_str());
            return 1;
        }
        if (!meta_jit->define_symbol("logos_qib_pack_blobs",
                                     reinterpret_cast<void*>(&logos_qib_pack_blobs))) {
            std::fprintf(stderr, "logosc: bind logos_qib_pack_blobs: %s\n",
                         meta_jit->error_str().c_str());
            return 1;
        }
        if (!meta_jit->define_symbol("logos_qib_free_blobs",
                                     reinterpret_cast<void*>(&logos_qib_free_blobs))) {
            std::fprintf(stderr, "logosc: bind logos_qib_free_blobs: %s\n",
                         meta_jit->error_str().c_str());
            return 1;
        }
        if (!meta_jit->define_symbol("logos_qib_pack_cursors",
                                     reinterpret_cast<void*>(&logos_qib_pack_cursors))) {
            std::fprintf(stderr, "logosc: bind logos_qib_pack_cursors: %s\n",
                         meta_jit->error_str().c_str());
            return 1;
        }
        if (!meta_jit->define_symbol("logos_qib_free_cursors",
                                     reinterpret_cast<void*>(&logos_qib_free_cursors))) {
            std::fprintf(stderr, "logosc: bind logos_qib_free_cursors: %s\n",
                         meta_jit->error_str().c_str());
            return 1;
        }
        if (!meta_jit->define_symbol("logos_metaprog_gensym",
                                     reinterpret_cast<void*>(&logos_metaprog_gensym))) {
            std::fprintf(stderr, "logosc: bind logos_metaprog_gensym: %s\n",
                         meta_jit->error_str().c_str());
            return 1;
        }
        if (!meta_jit->define_symbol("logos_metaprog_test_module_blob",
                                     reinterpret_cast<void*>(&logos_metaprog_test_module_blob))) {
            std::fprintf(stderr, "logosc: bind logos_metaprog_test_module_blob: %s\n",
                         meta_jit->error_str().c_str());
            return 1;
        }
        if (!meta_jit->define_symbol("logos_test_make_bin_op_blob",
                                     reinterpret_cast<void*>(&logos_test_make_bin_op_blob))) {
            std::fprintf(stderr, "logosc: bind logos_test_make_bin_op_blob: %s\n",
                         meta_jit->error_str().c_str());
            return 1;
        }
        if (!meta_jit->define_symbol("logos_quote_expr_subst",
                                     reinterpret_cast<void*>(&logos_quote_expr_subst))) {
            std::fprintf(stderr, "logosc: bind logos_quote_expr_subst: %s\n",
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

        // MC1.2: item-position metacall handling, woven into the metaprog loop
        // so synthesized items are visible to the final sema pass below.
        // metaprog_mode sema dispatches METACALL_ITEM (it's a module item, not a
        // fn body), so meta_prog.metacall_sites is populated. Two-step per iter:
        //   - emit thunk_source for new sites (logos_emit_source dedups);
        //     next iter will sema/JIT-compile the thunks.
        //   - lookup each thunk in this iter's meta_jit; if found, invoke
        //     (calls logos_emit_item_blob_subst → appends new AST + sets
        //     *g_any_emitted=true) and mark the call site METACALL_ITEM_DONE.
        {
            for (const auto& site : meta_item_sites) {
                if (site.thunk_source.empty()) continue;
                if (site.ast_idx >= asts.size()) continue;
                auto* sym = meta_jit->lookup(site.thunk_name);
                if (!sym) continue;  // thunk compiled in next iter
                reinterpret_cast<void (*)()>(sym)();
                auto& doc = asts[site.ast_idx];
                auto* h    = doc.holder();
                auto* base = h->base();
                auto* tom  = reinterpret_cast<logos::hermes::TinyObjectMap*>(
                                base + site.expr_offset);
                if (auto r = tom->put(
                        logos::compiler::ast::CODE.code,
                        logos::hermes::AnyVal::from_value<int32_t>(
                            logos::compiler::ast::METACALL_ITEM_DONE.code),
                        h->arena()); !r) {
                    std::fprintf(stderr,
                        "logosc: metacall item-splice (loop): CODE put failed\n");
                    return 1;
                }
            }
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

                // MC1.2: item-position metacall. Thunk is `unsafe fn() -> ()`
                // that internally calls `logos_emit_item_blob_subst(&blob)`,
                // which appends a fresh AST to g_asts. After invoke, mark the
                // METACALL_ITEM AST node consumed (CODE = METACALL_ITEM_DONE)
                // so the next sema pass skips it.
                if (site.ret_tag == RT::ItemBlob) {
                    reinterpret_cast<void (*)()>(sym)();
                    auto& doc = asts[site.ast_idx];
                    auto* h   = doc.holder();
                    auto* base = h->base();
                    auto* tom = reinterpret_cast<logos::hermes::TinyObjectMap*>(
                                    base + site.expr_offset);
                    if (auto r = tom->put(
                            logos::compiler::ast::CODE.code,
                            logos::hermes::AnyVal::from_value<int32_t>(
                                logos::compiler::ast::METACALL_ITEM_DONE.code),
                            h->arena()); !r) {
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

    // MC1.2: drop synthesised metacall thunks before final codegen — their
    // bodies reference JIT-only host shims (logos_emit_item_blob_subst,
    // logos_quote_expr_subst, …) which aren't defined in the user binary.
    prog.functions.erase(
        std::remove_if(prog.functions.begin(), prog.functions.end(),
            [](const auto& f) {
                return f && f->name.rfind("__metacall_thunk_", 0) == 0;
            }),
        prog.functions.end());

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
