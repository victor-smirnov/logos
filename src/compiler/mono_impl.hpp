// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// mono_impl.hpp — Mono class definition shared across mono_*.cpp TUs.

#pragma once

#include <logos/compiler/mono.hpp>
#include <logos/compiler/lir.hpp>
#include <logos/compiler/sema.hpp>

#include <format>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace logos::compiler {

using SubstMap = std::unordered_map<std::string, const LogosType*>;
using PackMap  = std::unordered_map<std::string, std::vector<const LogosType*>>;

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
    int            max_depth_;
    int            depth_ = 0;
    PackMap        cur_packs_;

    std::unordered_map<std::string, const lir::LFunction*>  templates_;
    std::unordered_map<std::string, std::vector<const lir::LFunction*>> specs_;
    std::unordered_map<std::string, const lir::LStructDef*> struct_templates_;
    std::unordered_map<std::string, std::vector<const lir::LStructDef*>> struct_specs_;
    std::unordered_map<std::string, std::pair<const LogosType*, int>> needed_struct_insts_;
    std::unordered_set<std::string> struct_done_;
    std::unordered_map<std::string, const lir::LEnumDef*>   enum_templates_;
    std::unordered_map<std::string, std::pair<std::vector<const LogosType*>, int>> needed_enum_insts_;
    std::unordered_set<std::string> enum_done_;
    std::unordered_map<std::string, const lir::LClassDef*>  class_templates_;
    std::unordered_map<std::string, std::pair<const LogosType*, int>> needed_class_insts_;
    std::unordered_set<std::string> class_done_;
    std::unordered_set<std::string> done_;
    std::unordered_map<std::string, const LogosType*> assoc_impls_;

    struct WorkItem {
        std::string                mangled;
        const lir::LFunction*      tmpl;
        SubstMap                   subst;
        PackMap                    packs;
        int                        depth;
    };
    std::vector<WorkItem> worklist_;

    // ── Type substitution (large — defined in mono_subst.cpp) ────────────
    const LogosType* subst_type(const LogosType* t, const SubstMap& s) noexcept;

    // ── Record needed instantiations (small — inline) ────────────────────
    void record_needed_struct(const LogosType* t) {
        if (!t || t->kind != LogosType::Kind::Struct || t->type_args.empty()) return;
        for (auto* a : t->type_args)
            if (a->kind == LogosType::Kind::TypeVar) return;
        auto cname = concrete_struct_name(t);
        if (!struct_done_.count(cname)) {
            if (depth_ >= max_depth_) {
                in_.diags.diags.push_back({Diag::Level::Error, "mono",
                    std::format("struct instantiation depth limit ({}) exceeded for '{}'",
                                max_depth_, cname), {}, 0});
                return;
            }
            needed_struct_insts_[cname] = {t, depth_ + 1};
        }
    }

    void record_needed_class(const LogosType* t) {
        if (!t || t->kind != LogosType::Kind::Class || t->type_args.empty()) return;
        for (auto* a : t->type_args)
            if (a->kind == LogosType::Kind::TypeVar) return;
        auto cname = concrete_class_name(t);
        if (!class_done_.count(cname)) {
            if (depth_ >= max_depth_) {
                in_.diags.diags.push_back({Diag::Level::Error, "mono",
                    std::format("class instantiation depth limit ({}) exceeded for '{}'",
                                max_depth_, cname), {}, 0});
                return;
            }
            needed_class_insts_[cname] = {t, depth_ + 1};
        }
    }

    void record_needed_enum(const LogosType* t) {
        if (!t || t->kind != LogosType::Kind::Enum || t->type_args.empty()) return;
        for (auto* a : t->type_args)
            if (a->kind == LogosType::Kind::TypeVar) return;
        std::string cname = t->enum_name;
        for (auto* a : t->type_args) { cname += "__"; cname += mangle_type(a); }
        if (!enum_done_.count(cname)) {
            if (depth_ >= max_depth_) {
                in_.diags.diags.push_back({Diag::Level::Error, "mono",
                    std::format("enum instantiation depth limit ({}) exceeded for '{}'",
                                max_depth_, cname), {}, 0});
                return;
            }
            needed_enum_insts_[cname] = {t->type_args, depth_ + 1};
        }
    }

    // ── Mangling (static — inline) ────────────────────────────────────────
    static std::string mangle_type(const LogosType* t) {
        if (!t) return "null";
        switch (t->kind) {
        case LogosType::Kind::Ptr:
            return (t->mut_ptr ? "pmut_" : "pcst_") + mangle_type(t->pointee);
        case LogosType::Kind::Ref:    return "ref_"    + mangle_type(t->pointee);
        case LogosType::Kind::MutRef: return "refmut_" + mangle_type(t->pointee);
        case LogosType::Kind::Array:
            return "arr" + std::to_string(t->arr_size) + "_" + mangle_type(t->elem);
        case LogosType::Kind::Struct:
            return concrete_struct_name(t);
        default:
            return type_str(t);
        }
    }

    static std::string mangle(const std::string& name,
                               const std::vector<const LogosType*>& type_args) {
        std::string result = name;
        for (auto* t : type_args) {
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

    // ── Struct/class/enum cloning (large — defined in mono_clone.cpp) ─────
    lir::LStructDef clone_struct_def(const lir::LStructDef& tmpl,
                                      const SubstMap& s,
                                      const PackMap& packs,
                                      const std::string& new_name);
    lir::LClassDef  clone_class_def(const lir::LClassDef& tmpl,
                                     const SubstMap& s,
                                     const PackMap& packs,
                                     const std::string& new_name);
    lir::LEnumDef   clone_enum_def(const lir::LEnumDef& tmpl,
                                    const SubstMap& s,
                                    const PackMap& packs,
                                    const std::string& new_name);

    // ── Scan (defined in mono_scan.cpp) ───────────────────────────────────
    void scan_fn(const lir::LFunction& fn);
    void scan_block(const lir::LBlock& b);
    void scan_stmt(const lir::LStmt& st);
    void scan_expr(const lir::LExpr& e);

    // ── Pattern matching (static — inline) ───────────────────────────────
    static bool match_type(const LogosType* concrete, const LogosType* pattern,
                           SubstMap& bindings) noexcept {
        if (!concrete || !pattern) return false;
        if (pattern->kind == LogosType::Kind::TypeVar) {
            auto it = bindings.find(pattern->type_var_name);
            if (it != bindings.end())
                return types_equal(*concrete, *it->second);
            bindings[pattern->type_var_name] = concrete;
            return true;
        }
        if (pattern->kind != concrete->kind) return false;
        switch (pattern->kind) {
        case LogosType::Kind::Ptr:
            return pattern->mut_ptr == concrete->mut_ptr &&
                   match_type(concrete->pointee, pattern->pointee, bindings);
        case LogosType::Kind::Ref:
        case LogosType::Kind::MutRef:
            return match_type(concrete->pointee, pattern->pointee, bindings);
        case LogosType::Kind::Array:
            return pattern->arr_size == concrete->arr_size &&
                   match_type(concrete->elem, pattern->elem, bindings);
        case LogosType::Kind::Struct:
            return pattern->struct_name == concrete->struct_name;
        default:
            return types_equal(*concrete, *pattern);
        }
    }

    static int type_specificity(const LogosType* t) noexcept {
        if (!t || t->kind == LogosType::Kind::TypeVar) return 0;
        if (t->kind == LogosType::Kind::Ptr)   return 1 + type_specificity(t->pointee);
        if (t->kind == LogosType::Kind::Array)  return 1 + type_specificity(t->elem);
        return 100;
    }

    static int specificity_score(const std::vector<const LogosType*>& patterns) noexcept {
        int s = 0;
        for (auto* p : patterns) s += type_specificity(p);
        return s;
    }

    // ── Spec selection (defined in mono_scan.cpp) ─────────────────────────
    const lir::LFunction*  find_best_spec(const std::string& base_name,
                                          const std::vector<const LogosType*>& type_args);
    const lir::LStructDef* find_best_struct_spec(const std::string& base_name,
                                                  const std::vector<const LogosType*>& type_args);

    // ── Enqueue / instantiate (defined in mono_scan.cpp) ─────────────────
    void enqueue_if_needed(const std::string& mangled_callee,
                           const std::vector<const LogosType*>& type_args);

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
    void collect_type_for_structs(const LogosType* t) {
        if (!t) return;
        switch (t->kind) {
        case LogosType::Kind::Ptr:
        case LogosType::Kind::Ref:
        case LogosType::Kind::MutRef:   collect_type_for_structs(t->pointee); break;
        case LogosType::Kind::Array: collect_type_for_structs(t->elem);    break;
        case LogosType::Kind::Struct:
            record_needed_struct(t);
            for (auto* a : t->type_args) collect_type_for_structs(a);
            break;
        default: break;
        }
    }

    void collect_type_for_classes(const LogosType* t) {
        if (!t) return;
        switch (t->kind) {
        case LogosType::Kind::Ptr:
        case LogosType::Kind::Ref:
        case LogosType::Kind::MutRef:   collect_type_for_classes(t->pointee); break;
        case LogosType::Kind::Array: collect_type_for_classes(t->elem);    break;
        case LogosType::Kind::Class:
            record_needed_class(t);
            for (auto* a : t->type_args) collect_type_for_classes(a);
            break;
        default: break;
        }
    }

    // ── Struct/enum/class needs collection (defined in mono_clone.cpp) ────
    void collect_struct_needs_from_output();
    void collect_struct_needs_from_block(const lir::LBlock& b);
    void collect_struct_needs_from_stmt(const lir::LStmt& st);
    void collect_struct_needs_from_expr(const lir::LExpr& e);

    // ── Instantiation (defined in mono_clone.cpp) ─────────────────────────
    void instantiate_struct_templates();
    void instantiate_enum_templates();
    void instantiate_class_templates();
    void instantiate_one_class(const std::string& cname, const LogosType* class_t, int depth);
};

} // namespace logos::compiler
