//
// emit_module — build a binary Logos module (.a archive) from a manifest.
//
// Output: libNAME.a containing
//   NAME.o       — compiled non-generic code for the whole module
//   NAME.writ0 — binary AST dump (for sema on client side)

#include "emit_module.hpp"
#include "compile_pipeline.hpp"
#include "metaprog_dispatch.hpp"
#include "module_loader.hpp"

#include <logos/compiler/ast.hpp>

#include <climits>
#include <unistd.h>

#include <logos/compiler/borrow_check.hpp>
#include <logos/compiler/lir.hpp>
#include <logos/compiler/sema.hpp>   // type_str (ABI layout sidecar)
#include <logos/compiler/lir_mirror.hpp>
#include <logos/compiler/mono.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>

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

// ── ABI-spec scoping: the WQL/Trama query-engine internals ──────────────────
// The whole `logos.std.wql.*` package tree — IR schemas (SExpr/RExpr/RQuery/
// TStmt), the peg-generated parsers, plan_walker/rexpr_walk, optimize, codegen,
// reflect, el/trama internals — is IMPLEMENTATION DETAIL. The only consumer-
// facing surface is the macros (wql!/trama!/wql_walk!), which are #[token_macro]
// and are ALREADY dropped by is_macro_hook(). A consumer never links a
// logos.std.wql.* symbol: the macro expands into the CONSUMER's module using
// Vec/String from OTHER packages. So the query engine is not link-time ABI, and
// its churn (handler sigs, walk_query/walk_query_any, IR-schema layouts) keeps
// falsely tripping the abi-gate as ABI-breaking. Exclude the package tree from
// the .abi-pub allowlist AND from the .abi-layout type/vtable records.
//
// Matched on the PACKAGE precisely (not a substring of a mangled symbol): the
// fn/struct/enum/trait views carry `package()`/`pkg()` directly, so we test the
// package == "logos.std.wql" or has the "logos.std.wql." prefix (a sub-package
// like logos.std.wql.ir / .optimize / .wql). This will NOT match an unrelated
// package that merely embeds the substring.
static bool is_wql_internal_pkg(std::string_view pkg) {
    static constexpr std::string_view kRoot = "logos.std.wql";
    if (pkg == kRoot) return true;
    return pkg.size() > kRoot.size() && pkg.rfind(kRoot, 0) == 0 &&
           pkg[kRoot.size()] == '.';
}

// ── ABI-spec scoping: the Deem query/reasoning-engine internals ──────────────
// logos.std.deem holds the queue-2 interpreter AND the DBSP incremental engine
// (IncrJoin / IncrAnti / IncrRec / AggState / FactStore / Arr / ZBatch / ZOut /
// QRelReg / RelCtx / …) — heavy-development internals that grow a type layout or
// add a whole type on essentially every incremental slice, falsely tripping the
// abi-gate as ABI-breaking (the same problem the wql engine had). The ONLY
// consumer-facing contract is the DYNAMIC-QUERY API: compile a query text
// against a SchemaCatalog, bind sources into a QEnv, run, read a QRows (QError
// on failure). Everything else in the package is implementation detail.
// Expressed as an ALLOWLIST (not a blocklist) so a NEW engine type is excluded
// automatically — the churn never reaches the spec, no per-slice bump. Matched
// on the exact TYPE name within the deem package (methods emit per-struct, so an
// excluded type drops its methods wholesale; free helpers are private already).
static bool is_deem_api_type(std::string_view name) {
    return name == "Query" || name == "SchemaCatalog" || name == "QEnv" ||
           name == "QRows" || name == "QError";
}
static bool is_deem_internal_type(std::string_view pkg, std::string_view name) {
    static constexpr std::string_view kRoot = "logos.std.deem";
    const bool in_deem = (pkg == kRoot) ||
        (pkg.size() > kRoot.size() && pkg.rfind(kRoot, 0) == 0 && pkg[kRoot.size()] == '.');
    return in_deem && !is_deem_api_type(name);
}

// Second leak form the package test can't see: generic INSTANCES parameterized
// by an excluded type. `type_id_of__g__void__RQWith` lives in logos.lang.any
// (its own package is public) and its signature never mentions the type — the
// type argument survives only as a mangling token. Such instances exist in the
// archive only because wql internals instantiated them; no consumer can even
// name the type, so they are spec noise that churns with every wql refactor.
// Detect them by scanning the mangled symbol for an excluded type NAME at a
// mangling-token boundary (preceded by start/'_'/'$'/'.', followed by
// end/'_'/'$'/'.') — the boundary test keeps SExpr from matching inside
// SExprArr-the-different-token, and a non-excluded package that merely embeds
// the substring cannot produce a boundary hit for a mangled TYPE token.
static bool mentions_excluded_type(std::string_view sym,
                                   const std::vector<std::string>& names) {
    auto is_boundary = [](char c) {
        return c == '_' || c == '$' || c == '.';
    };
    for (const auto& n : names) {
        size_t pos = 0;
        while ((pos = sym.find(n, pos)) != std::string_view::npos) {
            bool l = pos == 0 || is_boundary(sym[pos - 1]);
            size_t end = pos + n.size();
            bool r = end == sym.size() || is_boundary(sym[end]);
            if (l && r) return true;
            pos += 1;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// .writ0 format (version 3)
//
//   magic[8]      "WRITAST0"
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
//                                     Load via writ::from_bytes_copy. The
//                                     blob holds the LIR Writ mirror
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

static bool write_writ0(const std::string& path,
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
    f.write("WRITAST0", 8);
    write_u32(f, 3);  // version 3: adds trailing exports section
    write_u32(f, static_cast<uint32_t>(modules.size()));

    for (auto& mod : modules) {
        auto enc = writ::binary_encode(mod.ast);
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
    auto add = [&](lir_view::FunctionView fn) {
        if (fn.is_extern()) return;
        if (fits(std::string(fn.source_file()))) return;
        lir_mirror_map_put_null(prog, prog.binary_symbols, fn.name());
    };
    for (auto& sd : prog.structs)
        for (auto& m : sd.methods()) add(m);
    for (auto& fn : prog.functions) add(fn);
}

// ── Doc-facts sidecar (--emit-docs, ADR 0014) ───────────────────────────────
// Sibling of the ABI-layout sidecar: walks the SAME post-sema decl views, but
// emits DOCUMENTATION facts (kind / qualified-path / name / parent / visibility
// / rendered-signature / doc-text) plus trait-impl edges, as a Writ-SDN document
// that `lforge doc` loads as a Deem EDB. Doc text is read from the `.doc()`
// accessors sema already populated from /// //! /** */ comments — the extractor
// never re-lexes. Whole-module (no per-file filter): documentation wants the full
// surface + cross-references resolved together, so `lforge doc` runs one emit per
// package rather than per source file.
static void emit_docs_facts(lir::LProgram& prog,
                            const std::string& package,
                            const std::string& docs_path) {
    const auto* pool = prog.type_pool.impl();
    auto qual = [](std::string_view pkg, std::string_view name) {
        return pkg.empty() ? std::string(name)
                           : std::string(pkg) + "." + std::string(name);
    };
    // `#[repr(transparent)]` UnsafeCell<X> renders as X (mirrors abi_type — the
    // interior-mutability wrapper is not part of the documented signature).
    auto rtype = [&](TypeRef t0) -> std::string {
        TypeRef t{t0};
        while (t && t.kind() == LogosType::Kind::Struct &&
               t.struct_name() == "UnsafeCell" && t.pkg_name() == "logos.lang.cell" &&
               !t.type_args().empty())
            t = TypeRef(t.type_args()[0]);
        return type_str(t);
    };
    // SDN string escaping — byte-for-byte the inverse of parse_writ (matches
    // writ::quote in src/writ/stringify.cpp).
    auto q = [](std::string_view s, std::string& out) {
        out += '"';
        for (char c : s) {
            switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r': out += "\\r";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf;
                } else out += c;
            }
        }
        out += '"';
    };

    std::string out;
    out += "{\n  package: ";
    q(package, out);
    out += ",\n  items: [\n";

    bool first_item = true;
    auto item = [&](std::string_view kind, const std::string& path,
                    std::string_view name, std::string_view parent,
                    bool is_pub, const std::string& sig, std::string_view doc,
                    std::string_view src_file) {
        if (!first_item) out += ",\n";
        first_item = false;
        out += "    {kind: ";  q(kind, out);
        out += ", path: ";     q(path, out);
        out += ", name: ";     q(name, out);
        out += ", parent: ";   q(parent, out);
        out += ", vis: ";      q(is_pub ? "pub" : "priv", out);
        out += ", sig: ";      q(sig, out);
        out += ", doc: ";      q(doc, out);
        out += ", src_file: "; q(src_file, out);
        out += "}";
    };
    // Render "fn name(p: T, …) -> Ret" from a function/method view. `nm` is the
    // SOURCE name (FunctionView::name() is the mangled link symbol; the un-mangled
    // name lives in method_base()).
    auto fn_sig = [&](auto& f, std::string_view nm) {
        std::string sig = "fn " + std::string(nm) + "(";
        bool pf = true;
        f.each_param([&](auto p) {
            if (!pf) sig += ", ";
            sig += std::string(p.name()) + ": " + rtype(p.type(pool));
            pf = false;
        });
        sig += ")";
        TypeRef rt = f.ret_type(pool);
        if (rt) sig += " -> " + rtype(rt);
        return sig;
    };

    // Strip refs / pointers / `mut` / type-args off a rendered type, leaving the
    // bare owner name (`&mut Result<T,E>` → `Result`) for the method→parent link.
    auto bare_type = [](std::string s) {
        size_t i = 0;
        while (i < s.size() && (s[i] == '&' || s[i] == '*' || s[i] == ' ')) i++;
        if (s.compare(i, 4, "mut ") == 0) i += 4;
        s = s.substr(i);
        if (auto lt = s.find('<'); lt != std::string::npos) s = s.substr(0, lt);
        return s;
    };
    // Fallback owner extraction from the mangled link name, for `self`-less
    // associated fns (e.g. `Vec::new`). Methods mangle as
    // "<pkg>.<Owner>__<mbase>__f__…" (no '$' before "__f__"); free fns as
    // "<hash>.<pkg>$<name>__f__…" ('$' before "__f__"). Returns "" for a free fn.
    // Generic-type methods carry a '$' in the head and are NOT caught here — those
    // are recognised by their `self` param instead.
    auto owner_from_mangle = [](std::string_view mangled, std::string_view mbase) -> std::string {
        if (mbase.empty()) return {};
        auto fpos = mangled.find("__f__");
        std::string_view head = (fpos == std::string_view::npos) ? mangled
                                                                 : mangled.substr(0, fpos);
        if (head.find('$') != std::string_view::npos) return {};
        std::string suffix = "__" + std::string(mbase);
        if (head.size() <= suffix.size() ||
            head.compare(head.size() - suffix.size(), suffix.size(), suffix) != 0)
            return {};
        std::string_view before = head.substr(0, head.size() - suffix.size());
        auto dot = before.rfind('.');
        return std::string(dot == std::string_view::npos ? before : before.substr(dot + 1));
    };
    // Functions — free fns AND impl/inherent methods all live in prog.functions
    // pre-mono. name() is the mangled link symbol; the source name is method_base().
    // Method discriminant: a `self` first param (owner = its type — works for
    // generic owners), else the mangled-name fallback (self-less associated fns).
    for (auto& fn : prog.functions) {
        if (!fn) continue;
        if (fn.is_extern() || fn.is_metaprog_stub()) continue;
        // Macro hooks (#[token_macro]/#[fn_macro]) ARE consumer surface — the
        // macro `<name>!(…)` is how users invoke them — even inside wql-internal
        // packages (the deem!/trama! handlers live there). Emit as kind "macro"
        // BEFORE the wql-internal filter; everything else in those packages
        // stays excluded.
        if (fn.is_macro_hook()) {
            std::string_view mnm = fn.method_base().empty() ? fn.name() : fn.method_base();
            item("macro", qual(fn.package(), mnm), mnm, "", fn.is_pub(),
                 "macro " + std::string(mnm) + "!", fn.doc(), fn.source_file());
            continue;
        }
        if (is_wql_internal_pkg(fn.package())) continue;
        std::string_view nm = fn.method_base().empty() ? fn.name() : fn.method_base();
        std::string_view first_pname;
        TypeRef first_ptype;
        fn.each_param([&, seen = false](auto p) mutable {
            if (seen) return;
            seen = true; first_pname = p.name(); first_ptype = p.type(pool);
        });
        std::string owner;
        if (first_pname == "self")
            owner = bare_type(type_str(first_ptype));
        if (owner.empty())
            owner = owner_from_mangle(fn.name(), fn.method_base());
        if (!owner.empty()) {
            std::string parent = (owner.find('.') != std::string::npos)
                                     ? owner : qual(fn.package(), owner);
            std::string path = parent + "::" + std::string(nm);
            item("method", path, nm, parent, fn.is_pub(), fn_sig(fn, nm),
                 fn.doc(), fn.source_file());
        } else {
            std::string path = qual(fn.package(), nm);
            item("fn", path, nm, "", fn.is_pub(), fn_sig(fn, nm),
                 fn.doc(), fn.source_file());
        }
    }
    // Structs / unions (+ fields). Methods come from prog.functions above
    // (sd.methods() is not populated at this pre-mono point).
    for (auto& sd : prog.structs) {
        if (is_wql_internal_pkg(sd.pkg()) || is_deem_internal_type(sd.pkg(), sd.name())) continue;
        std::string path = qual(sd.pkg(), sd.name());
        item(sd.is_union() ? "union" : "struct", path, sd.name(), "", sd.is_pub(),
             std::string(sd.is_union() ? "union " : "struct ") + std::string(sd.name()),
             sd.doc(), "");
        for (auto& fv : sd.fields())
            item("field", path + "::" + std::string(fv.name()), fv.name(), path, sd.is_pub(),
                 std::string(fv.name()) + ": " + rtype(fv.type(pool)), fv.doc(), "");
    }
    // Enums (+ variants).
    for (auto& ed : prog.enums) {
        if (is_wql_internal_pkg(ed.pkg()) || is_deem_internal_type(ed.pkg(), ed.name())) continue;
        std::string path = qual(ed.pkg(), ed.name());
        item("enum", path, ed.name(), "", ed.is_pub(),
             "enum " + std::string(ed.name()), ed.doc(), "");
        ed.each_variant([&](auto v) {
            std::string sig = std::string(v.name());
            if (v.has_payload()) {
                sig += "(";
                bool pf = true;
                for (auto pt : v.payload_types(pool)) {
                    if (!pf) sig += ", ";
                    sig += rtype(pt);
                    pf = false;
                }
                sig += ")";
            }
            item("variant", path + "::" + std::string(v.name()), v.name(), path,
                 ed.is_pub(), sig, "", "");
        });
    }
    // Traits (item + doc; per-method items are a fast follow once TraitView's
    // method-sig iteration is wired — the implementor edges below already carry
    // the key trait relation).
    for (auto& td : prog.traits) {
        if (is_wql_internal_pkg(td.pkg()) || is_deem_internal_type(td.pkg(), td.name())) continue;
        std::string path = qual(td.pkg(), td.name());
        item("trait", path, td.name(), "", /*is_pub=*/true,
             "trait " + std::string(td.name()), td.doc(), "");
    }

    out += "\n  ],\n  impls: [\n";
    bool first_impl = true;
    for (auto& iv : prog.impls) {
        if (iv.trait_name().empty()) continue;   // inherent impl — no implementor edge
        if (!first_impl) out += ",\n";
        first_impl = false;
        out += "    {trait: "; q(iv.trait_name(), out);
        out += ", type: ";     q(iv.target_type(), out);
        out += "}";
    }
    out += "\n  ]\n}\n";

    std::ofstream df(docs_path);
    df << out;
}

static bool compile_to_object(std::vector<writ::Writ>& asts,
                               std::vector<std::string>& filenames,
                               const std::vector<bool>& ast_only_flags,
                               const std::vector<bool>& from_binary_module_flags,
                               const std::string& obj_path,
                               const std::string& only_file = "",
                               StdlibExports* out_exports = nullptr,
                               std::vector<uint8_t>* out_lir_blob = nullptr,
                               const std::string& module_name = "",
                               const std::string& module_id = "",
                               const std::string& implicit_prelude = "",
                               const std::vector<std::string>& dep_archives = {},
                               const std::vector<std::string>& per_ast_module_ids = {},
                               const std::unordered_map<std::string, std::string>& module_name_to_id = {},
                               const std::string& abi_layout_path = "",
                               const std::string& docs_path = "",
                               int opt_level = 0,
                               bool overflow_checks = true,
                               const std::string& target_cpu = "generic") {
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
    // Module system: mutable copy of the per-AST module ids. Metaprog dispatch
    // grows `asts` and the dispatcher appends matching ids (the own module_id)
    // so this stays parallel to `asts` for the final sema pass below.
    std::vector<std::string> module_ids = per_ast_module_ids;

    // Collect the `nm --defined-only` symbol set of the dependency archives
    // ONCE, up front. It's the skeleton-skip gate (sema skips lowering bodies
    // of from_binary fns already compiled into a dep .o) AND the codegen
    // forward-declare gate (mlir_gen's is_binary_skip keys on
    // prog.binary_symbols). Collected before sema so the gate is populated by
    // the time bodies are lowered. (Was collected post-sema — too late for the
    // sema gate, which previously relied on the LIR-blob lookup_export proxy.)
    StrSet dep_symbols;
    // A manifest with NO `depends` lines leaves this gate EMPTY while -L still
    // loads binary-module ASTs — sema then silently RECOMPILES every dependency
    // body from the archived ASTs (slow), and the metaprog JIT of macro-heavy
    // closures (wql) breaks on unresolved recompiled calls. That failure mode
    // cost a debugging session; warn loudly instead of degrading in silence.
    if (dep_archives.empty()) {
        bool any_binary = false;
        for (bool fb : from_binary_module_flags)
            if (fb) { any_binary = true; break; }
        if (any_binary)
            std::fprintf(stderr,
                "logosc: warning: --emit-module manifest has no `depends` "
                "lines, but binary modules were loaded via -L/-l — dependency "
                "bodies will be RECOMPILED from their archived ASTs instead of "
                "linked. Add `depends <name>` per dependency archive "
                "(e.g. `depends logos-std`).\n");
    }
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
        mopts.module_ids     = &module_ids;  // module system: parallel to asts; grows with it
        mopts.self_module_id = module_id;    // hook-appended asts belong to THIS module
        mopts.module_name_to_id = module_name_to_id;  // §B-coex: `use … from` in discovery
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
                        // Exclude *_fibers.a: fiber_ctx.S carries initial-exec
                        // TLS relocations (R_X86_64_GOTTPOFF) that ORC's
                        // RuntimeDyld can't relocate. The metaprog JIT resolves
                        // the fiber hooks from liblstdlib_rt.a's weak stubs
                        // (metaprog_stubs.c) instead. (Mirrors is_jit_unsafe_archive
                        // in main.cpp — the metacall path filtered, this one didn't.)
                        if ((fn.rfind("liblstdlib", 0) == 0 ||
                             fn.rfind("liblogos-", 0) == 0) &&
                            ent.path().extension() == ".a" &&
                            fn.find("_fibers.a") == std::string::npos) {
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
                            // Exclude *_fibers.a (GOTTPOFF; see above).
                            if ((fn.rfind("liblstdlib", 0) == 0 ||
                                 fn.rfind("liblogos-", 0) == 0) &&
                                ent.path().extension() == ".a" &&
                                fn.find("_fibers.a") == std::string::npos) {
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
        // .writ0 member). Pre-fix, this was silently set to false →
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
    // dep_symbols (nm of the `depends`-archive .o files) is the skeleton-skip
    // gate: dep fns already in those .o have their bodies skipped + linked,
    // while this layer's own (not-yet-compiled) fns are absent → lowered
    // locally so mono's scan_fn sees their generic calls.
    SemaOptions sema_opts;
    sema_opts.implicit_prelude = implicit_prelude;
    sema_opts.binary_symbols = dep_symbols;  // skeleton-skip gate
    sema_opts.module_name_to_id = module_name_to_id;  // §3/§B-coex: resolve `use … from`
    auto prog = sema_lower(asts, filenames, from_binary, sema_opts, {}, module_ids);
    prog.print_diags(stderr);
    if (!prog.ok()) return false;

    // ── ABI layout sidecar (cat-2 type layouts + cat-3 vtables) ──────────────
    // sema_lower gives prog.structs/enums/traits as decl views right here. Emit
    // the layout-DETERMINING signature — a struct's ordered field types, an
    // enum's variants, a trait's vtable slot order — to <obj>.abi-layout, which
    // `logosc --emit-abi` globs and merges. A reorder/retype/add/remove changes
    // the record, so the ABI differ flags the break (symbols alone miss a field
    // reorder). Non-generic only: a template's layout is per-instantiation.
    // ── Shared ABI-spec pub-scoping helpers (layout + pub-allowlist blocks) ──
    // A mirror may miss the IS_PUB flag on side lowering paths (explicit
    // specializations like `pub struct WArray<WAny>` lower outside
    // lower_struct_def). Err toward INCLUSION: a type is treated pub when its
    // flag is set OR a same-BARE-named pub type exists (bare = instance name
    // up to the `$G` mangle marker). A false drop removes real API from the
    // spec; a false keep is only noise.
    std::set<std::string, std::less<>> abi_pub_type_names;
    for (auto& sd : prog.structs)
        if (sd.is_pub()) abi_pub_type_names.insert(std::string(sd.name()));
    for (auto& ed : prog.enums)
        if (ed.is_pub()) abi_pub_type_names.insert(std::string(ed.name()));
    auto abi_bare_template = [](std::string_view n) {
        auto g = n.find("$G");
        return g == std::string_view::npos ? n : n.substr(0, g);
    };
    auto abi_treat_pub = [&](std::string_view name, bool flag) {
        return flag || abi_pub_type_names.count(abi_bare_template(name)) > 0;
    };
    if (!module_name.empty() && !abi_layout_path.empty()) {
        const auto* pool = prog.type_pool.impl();
        auto qual = [](std::string_view pkg, std::string_view name) {
            return pkg.empty() ? std::string(name)
                               : std::string(pkg) + "." + std::string(name);
        };
        // `#[repr(transparent)]` UnsafeCell<X> has the EXACT ABI of X (same size/
        // align/offsets/calling-convention). Unwrap it for the spec so that the
        // interior-mutability refactor — wrapping a field in UnsafeCell to mark it
        // mutable-through-`&` — does NOT read as an ABI break (it isn't). UnsafeCell
        // is the canonical transparent interior-mut lang-item, recognized by its
        // qualified name (mirrors sema_auto_trait / type_is_freeze). Nested wraps
        // unwrap iteratively.
        auto abi_type = [&](TypeRef t0) -> std::string {
            TypeRef t{t0};
            while (t && t.kind() == LogosType::Kind::Struct &&
                   t.struct_name() == "UnsafeCell" && t.pkg_name() == "logos.lang.cell" &&
                   !t.type_args().empty())
                t = TypeRef(t.type_args()[0]);
            return type_str(t);
        };
        std::ofstream af(abi_layout_path);
        // Structs (generic templates INCLUDED): a template's ordered field types
        // — expressed in its type-var names (Vec<T> → ptr:*mut T,len:u64,…) —
        // determine every instantiation's layout, so a reorder/retype of the
        // template is the ABI break at its source. Field offsets need LLVM
        // materialisation, but the field-type list is the layout determinant.
        for (auto& sd : prog.structs) {
            if (is_wql_internal_pkg(sd.pkg())) continue;  // wql engine is internal
            if (is_deem_internal_type(sd.pkg(), sd.name())) continue;  // deem engine internal (only the Query API is contract)
            if (!abi_treat_pub(sd.name(), sd.is_pub())) continue;  // private type: not consumer ABI
            std::string rec = "type\t" + qual(sd.pkg(), sd.name()) + "\tfields=[";
            bool first = true;
            for (auto f : sd.fields()) {
                if (!first) rec += ",";
                rec += std::string(f.name()) + ":" + abi_type(f.type(pool));
                first = false;
            }
            af << rec << "]\n";
        }
        // Enums (templates included): variant name + payload types (a payload
        // type change, e.g. Some(i32)→Some(i64), changes the layout).
        for (auto& ed : prog.enums) {
            if (is_wql_internal_pkg(ed.pkg())) continue;  // wql engine is internal
            if (is_deem_internal_type(ed.pkg(), ed.name())) continue;  // deem engine internal (only the Query API is contract)
            if (!abi_treat_pub(ed.name(), ed.is_pub())) continue;  // private type: not consumer ABI
            std::string rec = "type\t" + qual(ed.pkg(), ed.name()) + "\tvariants=[";
            bool first = true;
            ed.each_variant([&](lir_view::EnumVariantView v) {
                if (!first) rec += ",";
                rec += std::string(v.name());
                if (v.has_payload()) {
                    rec += "(";
                    bool pf = true;
                    for (auto pt : v.payload_types(pool)) {
                        if (!pf) rec += ",";
                        rec += abi_type(pt);
                        pf = false;
                    }
                    rec += ")";
                }
                first = false;
            });
            af << rec << "]\n";
        }
        for (auto& td : prog.traits) {
            if (is_wql_internal_pkg(td.pkg())) continue;  // wql engine is internal
            if (is_deem_internal_type(td.pkg(), td.name())) continue;  // deem engine internal (only the Query API is contract)
            std::string rec = "vtable\t" + qual(td.pkg(), td.name()) + "\tslots=[";
            bool first = true;
            for (auto& [owner, mname] : td.vtable_method_order()) {
                (void)owner;
                if (!first) rec += ",";
                rec += std::string(mname);
                first = false;
            }
            af << rec << "]\n";
        }
    }

    // ── Doc-facts sidecar (--emit-docs) ─────────────────────────────────────
    // Same post-sema `prog` the ABI-layout block just walked; write the doc EDB
    // before mono_pass moves prog away.
    if (!module_name.empty() && !docs_path.empty())
        emit_docs_facts(prog, module_name, docs_path);

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
        const uint8_t*             body_addr = nullptr;  // stable mirror address
    };
    std::vector<TemplateEntry> generic_fn_templates;
    std::vector<TemplateEntry> generic_method_templates;
    auto stash_template = [](std::vector<TemplateEntry>& dst, lir_view::FunctionView fn) {
        if (fn.is_extern()) return;
        // Phase 5.B step 3 (Phase 5.C close-out): the prior
        // `if (fn.from_binary_module) return` here was wrong during the
        // stdlib build for ast_only files (e.g. std.compiler.metaprog) —
        // those get stamped from_binary=true post-metaprog-dispatch but
        // their bodies came from THIS sema's source, so there's no other
        // archive providing them. The mirror_ptr_ guard below catches
        // the genuine "already published" case (mirror missing means
        // body never lowered locally → nothing to publish).
        auto b = fn.body();
        if (!b) return;
        dst.push_back({std::string(fn.name()), b.addr()});
    };
    for (auto& fn : prog.functions) {
        if (fn && !fn.type_params_empty()) stash_template(generic_fn_templates, fn);
    }
    for (auto& sd : prog.structs) {
        if (sd.type_params_empty()) continue;
        for (auto& m : sd.methods()) {
            if (m) stash_template(generic_method_templates, m);
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
            if (!sd.type_params_empty())
                out_exports->struct_templates.push_back(
                    {std::string(sd.pkg()), std::string(sd.name())});
        for (auto& ed : prog.enums)
            if (!ed.type_params_empty())
                out_exports->enum_templates.push_back(
                    {std::string(ed.pkg()), std::string(ed.name())});
        for (auto& fn : prog.functions)
            if (!fn.type_params_empty())
                out_exports->fn_templates.push_back(std::string(fn.name()));
        for (auto& impl : prog.impls) {
            if (impl.is_negative()) continue;
            if (impl.is_blanket()) {
                std::vector<std::string> eb;
                for (auto sv : impl.extra_bounds()) eb.emplace_back(sv);
                out_exports->blanket_impls.push_back({
                    std::string(impl.trait_name()), std::string(impl.bound_trait()),
                    std::move(eb)});
            } else {
                out_exports->concrete_impls.push_back({
                    std::string(impl.trait_name()), std::string(impl.target_type())});
            }
        }
    }

    // Pre-mono borrow check of generic templates (Rust parity: a generic never
    // instantiated is still checked). Done HERE — at each module's own build — so
    // the loaded stdlib's generics are checked once, by the layer that owns them;
    // downstream/user compiles skip from_binary_module fns (borrow_check) and need
    // not re-check them. THIS layer's own generics carry from_binary_module=false,
    // so they ARE checked; lower-layer deps are skipped (already checked at theirs).
    prog = borrow_check(std::move(prog), /*generic_templates_only=*/true);
    prog.print_diags(stderr);
    if (!prog.ok()) return false;

    // Mono (also emits L-IR Writ mirror; borrow_check reads via mirror)
    prog = mono_pass(std::move(prog));
    prog.print_diags(stderr);
    if (!prog.ok()) return false;

    // Borrow check
    prog = borrow_check(std::move(prog));
    prog.print_diags(stderr);
    if (!prog.ok()) return false;

    // ── ABI PUBLIC-symbol allowlist sidecar (`.abi-pub`) ─────────────────────
    // `logosc --emit-abi` records EVERY external stdlib symbol as a `sym` line,
    // but Logos gives module-PRIVATE fns EXTERNAL linkage too — so a private
    // helper's removal (a legit internal refactor) reads as an ABI break. To
    // scope the spec to the PUBLIC surface, we emit, next to the archive, the
    // exact mangled link-symbol of every PUBLIC item (pub free fn, pub method,
    // and their monomorphised instances — is_pub is now propagated through mono).
    // --emit-abi intersects the raw nm symbols with the union of these sidecars.
    //
    // CRITICAL: the name MUST match `nm` byte-for-byte, so we use the compiler's
    // OWN canonical mangler — `sym::link_name(fn, prog.pkg_module_ids)` — the
    // SAME call mlir_gen makes to name the emitted symbol (see mlir_gen's
    // link_name / is_binary_skip). Done POST-mono so generic pub instances (in
    // prog.functions) are included; non-generic methods stay under their struct.
    if (!module_name.empty() && !abi_layout_path.empty()) {
        std::string pub_path = abi_layout_path;
        const std::string layout_sfx = ".abi-layout";
        if (pub_path.size() >= layout_sfx.size() &&
            pub_path.compare(pub_path.size() - layout_sfx.size(),
                             layout_sfx.size(), layout_sfx) == 0)
            pub_path.replace(pub_path.size() - layout_sfx.size(),
                             layout_sfx.size(), ".abi-pub");
        else
            pub_path += ".abi-pub";
        std::ofstream pf(pub_path);
        // Bare names of every ABI-excluded struct/enum — the token set for the
        // generic-instance leak check (mentions_excluded_type). Two sources:
        // wql-internal packages, and NON-PUB types anywhere (a private type is
        // not consumer-nameable, so its generic instantiations — e.g.
        // type_id_of__g__void__<Private> in logos.lang — are spec noise that
        // churns with every engine refactor; the logos.std.query interpreter
        // engine surfaced this class). Collision guard: a private name that a
        // PUB type elsewhere also uses is NOT added — bare-name token matching
        // must never drop a public type's instantiations.
        std::vector<std::string> excluded_type_names;
        for (auto& sd : prog.structs) {
            if (is_wql_internal_pkg(sd.pkg()) || is_deem_internal_type(sd.pkg(), sd.name()))
                excluded_type_names.emplace_back(sd.name());
            else if (!abi_treat_pub(sd.name(), sd.is_pub()))
                excluded_type_names.emplace_back(sd.name());
        }
        for (auto& ed : prog.enums) {
            if (is_wql_internal_pkg(ed.pkg()) || is_deem_internal_type(ed.pkg(), ed.name()))
                excluded_type_names.emplace_back(ed.name());
            else if (!abi_treat_pub(ed.name(), ed.is_pub()))
                excluded_type_names.emplace_back(ed.name());
        }
        auto emit_pub = [&](lir_view::FunctionView fn) {
            if (!fn) return;
            // extern (C-ABI) symbols are the host/runtime boundary, not the
            // Logos public surface curated here; skip. Generic templates carry
            // type-params and never emit a symbol of their own (only instances
            // do) — skip them too (their pub instances are separate entries).
            if (fn.is_extern()) return;
            if (!fn.type_params_empty()) return;
            if (!fn.is_pub()) return;
            // Metaprog macro hooks (#[fn_macro]/#[token_macro]) are compiler-
            // invoked (discovered by attribute, called by the metaprog driver),
            // NOT a linkable consumer API. Their signatures churn as wql/trama
            // iterate, which would falsely trip the abi-gate as ABI-breaking, so
            // exclude them from the public ABI surface.
            if (fn.is_macro_hook()) return;
            // The whole logos.std.wql.* query engine is internal implementation
            // (is_wql_internal_pkg) — its only consumer surface is the macros,
            // already dropped above; a consumer never links a wql symbol. Exclude
            // it so query-engine refactors do not falsely trip the abi-gate.
            if (is_wql_internal_pkg(fn.package())) return;
            // Generic instances parameterized by an excluded type (the type
            // survives only in the mangling — see mentions_excluded_type).
            std::string ln = sym::link_name(fn, prog.pkg_module_ids);
            if (mentions_excluded_type(ln, excluded_type_names)) return;
            pf << ln << '\n';
        };
        for (auto& fn : prog.functions) emit_pub(fn);
        for (auto& sd : prog.structs) {
            // Method mirrors may lack package attribution of their own — a
            // method belongs to its struct's package, so an excluded struct's
            // methods are excluded wholesale (fixes e.g. the wql
            // <Type>__type_id__g__ref_T blanket-impl instances leaking in).
            if (is_wql_internal_pkg(sd.pkg())) continue;
            if (is_deem_internal_type(sd.pkg(), sd.name())) continue;  // deem engine internal — drop its methods (only the Query API is contract)
            // A NON-pub struct's methods are not consumer-callable API (the
            // type is unnameable outside its module) — spec noise. Uses the
            // shared abi_treat_pub include-on-ambiguity fallback.
            if (!abi_treat_pub(sd.name(), sd.is_pub())) continue;
            for (auto& m : sd.methods()) emit_pub(m);
        }
    }

    // M4 step 1: snapshot prog.type_pool arena bytes for the .writ0 LIR
    // blob section. This is the post-mono LIR Writ mirror — every
    // template/struct/fn/expr/stmt that mono produced lives here with its
    // mirror_ptr_ value referencing offsets in these very bytes. Loaded
    // user-side via writ::from_bytes_copy; future M4 steps add the cross-
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
                    auto doc = writ::Writ(writ::WritView(holder));
                    if (auto bld = writ::lir_arena_root_begin(
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
                            [&](const uint8_t* addr, uint32_t oid) {
                            if (addr == nullptr) return;
                            auto av = writ::AnyVal::from_value<uint32_t>(
                                oid, static_cast<uint8_t>(writ::type_hash::U24));
                            (void) writ::TinyMapView(
                                reinterpret_cast<writ::TinyObjectMap*>(
                                    const_cast<uint8_t*>(addr)), holder)
                                .put(lir_schema::stmt_keys::EXPORT_ID.code, av);
                        };
                        auto try_publish = [&](lir_view::FunctionView fn) {
                            if (fn.is_extern()) return;
                            if (fn.is_specialization()) return;
                            if (!fn.type_params_empty()) return;
                            if (fn.from_binary_module()) return;
                            auto fb = fn.body();
                            if (!fb) return;
                            writ::AnyVal av; av.set_ref(fb.addr());
                            if (auto r = writ::arena_publish_named(*bld, std::string(fn.name()), av)) {
                                stamp_export_id(fb.addr(), *r);
                                ++published;
                            }
                        };
                        // NOTE (precompile-generics revival): non-generic bodies
                        // are NOT published — the consumer already skips re-lowering
                        // them via skeleton-skip + links them from this layer's .o
                        // (binary_symbols). Only GENERIC TEMPLATES (below) can't be
                        // linked (only their instantiations land in the .o), so they
                        // are the only bodies a consumer must read cross-arena. This
                        // keeps the blob to template bodies + their reachable types.
                        (void) try_publish;
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
                            writ::AnyVal av; av.set_ref(tmpl.body_addr);
                            if (auto r = writ::arena_publish_named(*bld, tmpl.name, av)) {
                                stamp_export_id(tmpl.body_addr, *r);
                                ++published_tmpl;
                            }
                        }
                        for (auto& tmpl : generic_method_templates) {
                            writ::AnyVal av; av.set_ref(tmpl.body_addr);
                            if (auto r = writ::arena_publish_named(*bld, tmpl.name, av)) {
                                stamp_export_id(tmpl.body_addr, *r);
                                ++published_tmpl;
                            }
                        }
                        if (std::getenv("LOGOS_TRACE_PHASES")) {
                            std::fprintf(stderr,
                                "emit_module: published %zu non-generic + %zu "
                                "template body export(s) for module '%s'\n",
                                published, published_tmpl, module_name.c_str());
                        }
                        auto fin = writ::lir_arena_root_finalize(*bld);
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
                    auto src_view = writ::WritView(h);
                    if (auto cl = writ::compactify(src_view)) {
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
            if (!dumped) {
                // The raw dump ships head() bytes verbatim — only valid when the
                // MultiChunk arena never appended (single chunk). A multi-chunk
                // arena would truncate to the first chunk; fail loudly instead of
                // silently corrupting the .writ0 blob. (The compactify path above
                // is the multi-chunk-safe route; the legacy no-module-root path
                // simply doesn't support arenas that outgrew the initial chunk.)
                if (arena->chunk_count() > 1) {
                    std::fprintf(stderr,
                        "emit_module: cannot raw-dump a multi-chunk type-pool arena "
                        "(%zu chunks) without a LirArenaRoot to compactify from — "
                        "build with a module name so the blob is compactified.\n",
                        arena->chunk_count());
                    return false;
                }
                out_lir_blob->assign(chunk.data(), chunk.data() + chunk.used);
            }
            // lir_arena_root_finalize sealed the live type-pool arena to snapshot
            // it for the blob (compactify produced an independent copy above).
            // Downstream mlir_gen/codegen still allocates into this same arena, so
            // reopen it now that the blob bytes are captured.
            if (arena->is_sealed()) arena->unseal();
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
    for (auto& __s : dep_symbols) lir_mirror_map_put_null(prog, prog.binary_symbols, __s);

    // Shared lowering tail (mlir_gen → MLIR→LLVM → object).
    LowerEmitOpts lopts;
    lopts.opt_level = opt_level;    // honor -O from the CLI (0 = skip opt pipeline)
    lopts.overflow_checks = overflow_checks;  // honor -C overflow-checks=off
    lopts.target_cpu = target_cpu;            // honor -C target-cpu=
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
    //   regular  → both .o and .writ0.
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
    // sees its declared dependencies' .writ0 packages before any user
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

        // Stamp this module's OWN (source) files with the manifest identity so
        // downstream sema mangles their symbols under this module. Binary deps
        // already carry their own id (set in module_loader::visit_binary_module).
        std::string self_id = module_effective_id(manifest, output_path);
        for (auto& m : modules) {
            if (!m.from_binary_module) {
                m.module_id   = self_id;
                m.module_name = manifest.name;
            }
        }
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
    // `<output_path>.writ0` directly (no archive). In standard mode,
    // intermediate files go in a temp dir and `ar` builds the .a.
    std::string obj_path;
    std::string h0_path;
    if (!opts.only_file.empty()) {
        obj_path = output_path + ".o";
        h0_path  = output_path + ".writ0";
    } else {
        auto tmp_dir = fs::temp_directory_path() / ("logos_emit_" + manifest.name);
        std::error_code ec;
        fs::create_directories(tmp_dir, ec);
        obj_path = (tmp_dir / (manifest.name + ".o")).string();
        h0_path  = (tmp_dir / (manifest.name + ".writ0")).string();
    }

    // Build AST + filename arrays for codegen — skip ast_only modules.
    // .writ0 takes everything (incl. ast_only).
    //
    // Compile-to-object also needs ast_only modules in its asts vector
    // — the metaprog dispatch loop (#21 closure) JIT-compiles their
    // handler fn bodies + reads their `#[metaprog_handler]` triggers.
    // We stamp ast_only with from_binary=true after dispatch so sema's
    // post-dispatch pass treats those items as already-emitted (no
    // codegen for host-extern-using fns).
    std::vector<writ::Writ> asts;
    std::vector<std::string> filenames;
    std::vector<bool>        ast_only_flags;       // parallel to asts
    std::vector<bool>        from_binary_module_flags;  // parallel to asts
    std::vector<std::string> per_ast_module_ids;    // parallel to asts (owning-module mangle key)
    std::unordered_map<std::string, std::string> module_name_to_id;  // §B-coex: NAME→id for `from`
    std::vector<ParsedModule> modules_for_h0;
    for (auto& m : modules) {
        modules_for_h0.push_back({m.path, m.package, m.ast, false, {}, {}});  // Writ is copy-on-write safe
        bool ao = is_ast_only_path(m.path);
        filenames.push_back(m.path);
        ast_only_flags.push_back(ao);
        from_binary_module_flags.push_back(m.from_binary_module);
        per_ast_module_ids.push_back(m.module_id);  // own files: self_id (stamped above); deps: archive id
        if (!m.module_name.empty() && !m.module_id.empty())
            module_name_to_id.emplace(m.module_name, m.module_id);
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
        // out_lir_blob = nullptr: the LIR blob is no longer emitted. Consumer
        // sema skeleton-skips non-generic dep bodies via binary_symbols (nm of
        // the dep .o) and links them; it never reads a published body. Passing
        // nullptr also skips the (expensive) compactify pass. The exports
        // trailer is computed for the verbose summary but no longer written.
        // Module identifier baked into the .pkgi (and, downstream, into symbol
        // mangling): explicit manifest `id`, else a hash of the output archive
        // path. Computed once here; consumers READ it from the .pkgi rather than
        // re-deriving, so it stays consistent regardless of derivation.
        std::string module_id = module_effective_id(manifest, output_path);
        if (!compile_to_object(asts, filenames, ast_only_flags,
                               from_binary_module_flags, obj_path,
                               only_file_canon, &exports, /*out_lir_blob=*/&lir_blob,
                               /*module_name=*/manifest.name,
                               /*module_id=*/module_id,
                               /*implicit_prelude=*/manifest.prelude,
                               /*dep_archives=*/all_lib_files,
                               /*per_ast_module_ids=*/per_ast_module_ids,
                               /*module_name_to_id=*/module_name_to_id,
                               /*abi_layout_path=*/output_path + ".abi-layout",
                               /*docs_path=*/opts.emit_docs
                                   ? (std::string(output_path) + ".docwr")
                                   : std::string{},
                               /*opt_level=*/opts.opt_level,
                               /*overflow_checks=*/opts.overflow_checks,
                               /*target_cpu=*/opts.target_cpu)) {
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
    // appear in the .writ0 archive — otherwise downstream consumers
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
        // drop all but the first when the .writ0 is loaded by user.
        path += "#" + std::to_string(i - original_ast_count);
        std::string pkg;
        auto root_av = asts[i].root_object();
        if (root_av.tagged().is_pointer()) {
            auto root_map = root_av.as_tiny_map();
            if (root_map.has_key(logos::compiler::ast::NAME)) {
                auto nm = root_map.get(logos::compiler::ast::NAME.code);
                if (!nm.is_null() && nm.is_pointer()) {
                    pkg = std::string(writ::StringView(
                        nm, asts[i].holder()).view());
                }
            }
            if (root_map.has_key(logos::compiler::ast::mod::PATH_PARTS)) {
                auto pp = root_map.get(logos::compiler::ast::mod::PATH_PARTS.code);
                if (!pp.is_null() && pp.is_pointer()) {
                    auto arr = writ::as_array(pp, asts[i].holder());
                    for (uint64_t j = 0; j < arr.size(); ++j) {
                        auto part_av = arr.get(j);
                        if (!part_av.is_pointer()) continue;
                        auto part_map = writ::TinyMapView(
                            part_av, asts[i].holder());
                        if (part_map.has_key(logos::compiler::ast::NAME)) {
                            auto nm = part_map.get(logos::compiler::ast::NAME.code);
                            if (!nm.is_null() && nm.is_pointer()) {
                                pkg += ".";
                                pkg += std::string(writ::StringView(
                                    nm, asts[i].holder()).view());
                            }
                        }
                    }
                }
            }
        }
        modules_for_h0.push_back({path, pkg, asts[i], false, {}, {}});
    }

    // .writ0: in per-file mode, contains only the target file's AST.
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
        if (!write_writ0(h0_path, single, /*exports=*/nullptr, /*lir_blob=*/nullptr)) {
            std::fprintf(stderr, "emit_module: .writ0 write failed\n");
            return false;
        }
        // Per-file mode also wraps .writ0 → .writ0.o so lforge can
        // archive it without ld.lld emitting an "is neither ET_REL nor
        // LLVM bitcode" warning at downstream link time.
        // Match emit_module-mode naming: "<base>.wr0" (≤15 chars in ar).
        std::string h0_obj =
            h0_path.substr(0, h0_path.find_last_of('.')) + ".wr0";
        {
            std::ostringstream cmd;
            cmd << "objcopy -I binary -O elf64-x86-64 "
                << "--rename-section .data=.lwrit "
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
    // copy here was pure dead weight (~2.6 MB of lang+mem AST on std.wr0) that
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
            "emit_module: A-AST — keeping %zu own module(s), dropped %zu dep AST(s) from .writ0\n",
            own_modules_for_h0.size(), dropped_dep_asts);
        std::fprintf(stderr, "emit_module: writing → %s\n", h0_path.c_str());
    }
    uint64_t mflags = manifest.lazy ? module_flag::LAZY : 0;
    // No exports trailer, no LIR blob: the .wr0 now ships only the own-module
    // AST (signatures + generic templates + impls). Non-generic dep bodies
    // live in the dep .o and are linked; the consumer's sema skeleton-skips
    // them via binary_symbols.
    if (!write_writ0(h0_path, own_modules_for_h0,
                       /*exports=*/&exports, /*lir_blob=*/&lir_blob,
                       mflags)) {
        std::fprintf(stderr, "emit_module: .writ0 write failed\n");
        return false;
    }

    // Wrap .writ0 as a relocatable ELF object so ld.lld doesn't warn
    // about a non-ET_REL archive member when downstream binaries link
    // against this archive. The data lives in a non-ALLOC `.lwrit`
    // section; module_loader looks for that section by name.
    // Wrap into "<basename>.wr0" (must stay <=15 chars including the
    // trailing `/` separator that ar appends, otherwise the name spills
    // into the GNU extended name table — which our ar parser doesn't
    // chase).
    std::string h0_obj_path =
        h0_path.substr(0, h0_path.find_last_of('.')) + ".wr0";
    {
        std::ostringstream cmd;
        cmd << "objcopy -I binary -O elf64-x86-64 "
            << "--rename-section .data=.lwrit "
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
    // to know which packages each archive provides; reading the full .wr0
    // is ~30-40MB of memcpy per archive (filesystem cache is warm, but
    // alloc + zero-init + copy still costs ~40ms across 4-archive layer
    // builds). The .pkgi member ships the package list as ASCII (one
    // package per line) wrapped in an ELF .lpkgindex non-ALLOC section
    // (mirrors the .wr0 → .lwrit wrap so ld.lld doesn't warn about
    // non-ET_REL archive members). The streaming AR reader pulls it out
    // without touching .wr0 bytes. Member name must stay <=15 chars.
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
        // copy in .wr0 stays for sema self-containment but is never selected
        // over the owner.)
        // Header: the owning module's canonical name + mangle id (one module
        // per archive). Consumers map every package below to this id. The `@`
        // sigil keeps it out of the package list; legacy readers that predate
        // this skip `@`-lines (see parse_pkgi_member). Recomputed here (cheap,
        // deterministic — same inputs as the compile_to_object call site).
        if (!manifest.name.empty())
            f << "@module " << manifest.name << " "
              << module_effective_id(manifest, output_path) << "\n";
        // ABI stamp: the version of the compiler that built this archive. The
        // consumer's runtime reuse check (module_loader) refuses to use a binary
        // library built by a newer minor (one-directional compat) and exact-
        // matches pre-release/snapshot builds. `@`-sigil keeps it out of the
        // package list (parse_pkgi_member skips @-lines).
#ifdef LOGOS_VERSION_FULL
        f << "@abi " << LOGOS_VERSION_FULL << "\n";
#endif
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

    // Import-table member: a standalone Writ doc listing the libraries this
    // module imports (its `depends`), one (file_name, doc_name) per local
    // arena_id. Shipped as its own `.imp` member (wrapped in a `.limports`
    // ELF section, mirroring the .wr0/.pkgi wrap) so a tool can read just this
    // small member for fast dependency inspection, and so a cross-arena
    // ExternalRef's arena_id resolves through it. doc_name is "" today (one
    // document per .writ0; multi-doc reserved).
    std::string imp_obj_path;
    {
        std::vector<writ::ImportEntry> imports;
        imports.reserve(import_dep_archives.size());
        for (const auto& a : import_dep_archives) {
            imports.push_back({fs::path(a).filename().string(), std::string()});
        }
        auto blob = writ::build_import_table_blob(manifest.name, imports);
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
    // the .wr0 wrapper + pkgi index + import table. Eager mode also packs NAME.o.
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
