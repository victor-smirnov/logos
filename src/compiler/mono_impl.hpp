// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// mono_impl.hpp — Mono class definition shared across mono_*.cpp TUs.

#pragma once

#include <logos/compiler/mono.hpp>
#include <logos/compiler/lir.hpp>
#include <logos/compiler/lir_mirror.hpp>
#include <logos/compiler/lir_view.hpp>
#include <logos/compiler/sema.hpp>
#include <logos/compiler/str_map.hpp>

#include <format>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace logos::compiler {

using SubstMap = StrMap<TypeRef>;
using PackMap  = StrMap<std::vector<TypeRef>>;

static inline std::string make_pack_arg_name(std::string_view base, size_t idx) {
    return std::string("$pack_arg$") + std::string(base) + "$" + std::to_string(idx);
}

class Mono {
public:
    explicit Mono(int max_depth) : max_depth_(max_depth) {}

    lir::LProgram run(lir::LProgram&& in, int max_depth);

private:
    lir::LProgram  in_;
    lir::LProgram  out_;
    // Mirror of in_'s L-IR, populated at the start of run() so subst_*
    // passes can read sub-nodes via lir_view::PatRef/ExprRef. Offsets refer
    // into out_.type_pool.arena() (in_.type_pool is moved into out_ early).
    std::unique_ptr<LirMirrorTable>  in_mirror_;
    int            max_depth_;
    int            depth_ = 0;
    PackMap        cur_packs_;

protected:
    // Resolve an input Pattern* to its mirror PatRef. Returns null PatRef
    // when the mirror has no entry (caller falls back to variant access).
    lir_view::PatRef pat_ref_of(const lir::Pattern& p) const noexcept {
        if (!in_mirror_) return {};
        auto it = in_mirror_->pat.find(&p);
        if (it == in_mirror_->pat.end()) return {};
        return lir_view::PatRef(out_.type_pool.arena(), it->second);
    }
    lir_view::ExprRef expr_ref_of(const lir::LExpr& e) const noexcept {
        if (!in_mirror_) return {};
        auto it = in_mirror_->expr.find(&e);
        if (it == in_mirror_->expr.end()) return {};
        return lir_view::ExprRef(out_.type_pool.arena(), it->second);
    }
private:

    StrMap<const lir::LFunction*>  templates_;
    StrMap<std::vector<const lir::LFunction*>> specs_;
    StrMap<const lir::LStructDef*> struct_templates_;
    StrMap<std::vector<const lir::LStructDef*>> struct_specs_;
    StrMap<std::pair<TypeRef, int>> needed_struct_insts_;
    StrSet struct_done_;
    StrMap<const lir::LEnumDef*>   enum_templates_;
    StrMap<std::pair<std::vector<TypeRef>, int>> needed_enum_insts_;
    StrSet enum_done_;
    StrSet done_;
    StrMap<TypeRef> assoc_impls_;

    // Blanket impls indexed for AssocType resolution at mono time.
    // Entry: { trait, bound_trait, target_typevar, assoc_types_map }.
    struct BlanketImplInfo {
        std::string trait_name;
        std::string bound_trait;
        std::string target_typevar;
        StrMap<TypeRef> assoc_types;
    };
    std::vector<BlanketImplInfo> blanket_impls_;

    // Set of (trait::type) keys: concrete types that implement each trait.
    // Populated from out_.impls (non-blanket) so the blanket fallback can
    // verify a concrete type satisfies the bound.
    StrSet concrete_impls_;

    struct WorkItem {
        std::string                mangled;
        const lir::LFunction*      tmpl;
        SubstMap                   subst;
        PackMap                    packs;
        int                        depth;
    };
    std::vector<WorkItem> worklist_;

    // ── Type substitution (large — defined in mono_subst.cpp) ────────────
    TypeRef subst_type(TypeRef tv, const SubstMap& s) noexcept;

    // Pattern substitution — view-based walk over the input mirror.
    lir::Pattern subst_pattern(const lir::Pattern& pat, const SubstMap& s);

    // ── Record needed instantiations (small — inline) ────────────────────
    void record_needed_struct(TypeRef tr) {
        if (!tr || (tr.kind() != LogosType::Kind::Struct &&
                    tr.kind() != LogosType::Kind::ZonedStruct) ||
            tr.type_args().empty()) return;
        for (auto a : tr.type_args())
            if (TypeRef(a).kind() == LogosType::Kind::TypeVar) return;
        auto cname = concrete_struct_name(tr);
        if (!struct_done_.count(cname)) {
            if (depth_ >= max_depth_) {
                in_.diags.diags.push_back({Diag::Level::Error, "mono",
                    std::format("struct instantiation depth limit ({}) exceeded for '{}'",
                                max_depth_, cname), {}, 0});
                return;
            }
            needed_struct_insts_[cname] = {tr, depth_ + 1};
        }
    }

    void record_needed_enum(TypeRef tr) {
        if (!tr || tr.kind() != LogosType::Kind::Enum || tr.type_args().empty()) return;
        for (auto a : tr.type_args())
            if (TypeRef(a).kind() == LogosType::Kind::TypeVar) return;
        std::string cname = std::string(tr.enum_name());
        for (auto a : tr.type_args()) { cname += "__"; cname += mangle_type(a); }
        if (!enum_done_.count(cname)) {
            if (depth_ >= max_depth_) {
                in_.diags.diags.push_back({Diag::Level::Error, "mono",
                    std::format("enum instantiation depth limit ({}) exceeded for '{}'",
                                max_depth_, cname), {}, 0});
                return;
            }
            needed_enum_insts_[cname] = {tr.type_args(), depth_ + 1};
        }
    }

    // ── Mangling (static — inline) ────────────────────────────────────────
    static std::string mangle_type(TypeRef tr) {
        if (!tr) return "null";
        switch (tr.kind()) {
        case LogosType::Kind::Ptr:
            return (tr.mut_ptr() ? "pmut_" : "pcst_") + mangle_type(tr.pointee());
        case LogosType::Kind::Ref:    return "ref_"    + mangle_type(tr.pointee());
        case LogosType::Kind::MutRef: return "refmut_" + mangle_type(tr.pointee());
        case LogosType::Kind::Array:
            return "arr" + std::to_string(tr.arr_size()) + "_" + mangle_type(tr.elem());
        case LogosType::Kind::Struct:
            return concrete_struct_name(tr);
        default:
            return type_str(tr);
        }
    }

    static std::string mangle(const std::string& name,
                               const std::vector<TypeRef>& type_args) {
        std::string result = name;
        for (auto t : type_args) {
            result += "__";
            result += mangle_type(t);
        }
        return result;
    }

    // ── Expression/statement cloning (large — defined in mono_clone.cpp) ─
    lir::LExprPtr subst_expr(const lir::LExpr& e, const SubstMap& s,
                              const PackMap& /*unused*/ = {});
    lir::LStmt    subst_stmt(const lir::LStmt& st, const SubstMap& s);

    lir::LBlock subst_block(const lir::LBlock& b, const SubstMap& s,
                             const PackMap& /*unused*/ = {}) {
        lir::LBlock nb;
        for (auto& st : b.stmts)
            nb.stmts.push_back(subst_stmt(st, s));
        return nb;
    }

    lir::LFunction clone_fn(const lir::LFunction& fn, const SubstMap& s,
                             const PackMap& packs = {});

    // ── Struct/enum cloning (large — defined in mono_clone.cpp) ─────
    lir::LStructDef clone_struct_def(const lir::LStructDef& tmpl,
                                      const SubstMap& s,
                                      const PackMap& packs,
                                      const std::string& new_name);
    lir::LEnumDef   clone_enum_def(const lir::LEnumDef& tmpl,
                                    const SubstMap& s,
                                    const PackMap& packs,
                                    const std::string& new_name);

    // ── Scan (defined in mono_scan.cpp) ───────────────────────────────────
    // Mirror-dispatched: scan_fn looks up the function's body offset in
    // out_.mirror_table and walks through view types from there. This
    // requires lir_mirror_emit_function to have been called for `fn`
    // before scan_fn — see clone+push_back sites in mono.cpp.
    void scan_fn(const lir::LFunction& fn);
    void scan_block(lir_view::BlockRef b);
    void scan_stmt(lir_view::StmtRef s);
    void scan_expr(lir_view::ExprRef e);

    // ── Pattern matching (static — inline) ───────────────────────────────
    static bool match_type(TypeRef c, TypeRef p,
                           SubstMap& bindings) noexcept {
        if (!c || !p) return false;
        if (p.kind() == LogosType::Kind::TypeVar) {
            auto tvn = p.type_var_name();
            auto it = bindings.find(tvn);
            if (it != bindings.end())
                return types_equal(c, TypeRef(it->second));
            bindings[std::string(tvn)] = c;
            return true;
        }
        if (p.kind() != c.kind()) return false;
        switch (p.kind()) {
        case LogosType::Kind::Ptr:
            return p.mut_ptr() == c.mut_ptr() &&
                   match_type(c.pointee(), p.pointee(), bindings);
        case LogosType::Kind::Ref:
        case LogosType::Kind::MutRef:
            return match_type(c.pointee(), p.pointee(), bindings);
        case LogosType::Kind::Array:
            return p.arr_size() == c.arr_size() &&
                   match_type(c.elem(), p.elem(), bindings);
        case LogosType::Kind::Struct:
            return p.struct_name() == c.struct_name();
        default:
            return types_equal(c, p);
        }
    }

    static int type_specificity(TypeRef tr) noexcept {
        if (!tr || tr.kind() == LogosType::Kind::TypeVar) return 0;
        if (tr.kind() == LogosType::Kind::Ptr)   return 1 + type_specificity(tr.pointee());
        if (tr.kind() == LogosType::Kind::Array)  return 1 + type_specificity(tr.elem());
        return 100;
    }

    static int specificity_score(const std::vector<TypeRef>& patterns) noexcept {
        int s = 0;
        for (auto p : patterns) s += type_specificity(p);
        return s;
    }

    // Per-position specificity vector for lexicographic comparison.
    // Enables correct disambiguation of partial specs like Map<Bitmap,V> vs Map<K,AnyVal>
    // when both score equally by summed specificity but differ positionally.
    static std::vector<int> specificity_vec(const std::vector<TypeRef>& patterns) noexcept {
        std::vector<int> v;
        v.reserve(patterns.size());
        for (auto p : patterns) v.push_back(type_specificity(p));
        return v;
    }

    // ── Spec selection (defined in mono_scan.cpp) ─────────────────────────
    const lir::LFunction*  find_best_spec(const std::string& base_name,
                                          const std::vector<TypeRef>& type_args);
    const lir::LStructDef* find_best_struct_spec(const std::string& base_name,
                                                  const std::vector<TypeRef>& type_args);

    // ── Enqueue / instantiate (defined in mono_scan.cpp) ─────────────────
    void enqueue_if_needed(const std::string& mangled_callee,
                           const std::vector<TypeRef>& type_args);

    lir::LFunction instantiate_fn(const lir::LFunction& tmpl,
                                   const std::string& mangled_name,
                                   const SubstMap& subst,
                                   const PackMap& packs = {}) {
        ++depth_;
        auto fn = clone_fn(tmpl, subst, packs);
        fn.name = mangled_name;
        --depth_;
        return fn;
    }

    // ── Struct/class/enum type collection (inline) ────────────────────────
    void collect_type_for_structs(TypeRef tr) {
        if (!tr) return;
        switch (tr.kind()) {
        case LogosType::Kind::Ptr:
        case LogosType::Kind::Ref:
        case LogosType::Kind::MutRef:   collect_type_for_structs(tr.pointee()); break;
        case LogosType::Kind::Array: collect_type_for_structs(tr.elem());    break;
        case LogosType::Kind::Struct:
        case LogosType::Kind::ZonedStruct:
            record_needed_struct(tr);
            for (auto a : tr.type_args()) collect_type_for_structs(a);
            break;
        default: break;
        }
    }

    // ── Struct/enum needs collection (defined in mono_clone.cpp) ────
    void collect_struct_needs_from_output();
    void collect_struct_needs_from_block(const lir::LBlock& b);
    void collect_struct_needs_from_stmt(const lir::LStmt& st);
    void collect_struct_needs_from_expr(const lir::LExpr& e);

    // ── Instantiation (defined in mono_clone.cpp) ─────────────────────────
    void instantiate_struct_templates();
    void instantiate_enum_templates();
};

} // namespace logos::compiler
