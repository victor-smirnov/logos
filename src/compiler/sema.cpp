// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Semantic analysis + L-IR lowering — core file.
//
// This file contains:
//   - Free utility functions (types_equal, type_str, types_compatible, etc.)
//   - SemaChecker::run(), init_primitives(), lookup_type_by_name()
//   - Scope/drop helpers, type-param helpers, subst_type_sema, resolve_type
//   - lower_program(), lower_module_items(), sema_lower() entry point
//
// Method bodies for collect_*, lower_expr/stmt/decl are in sema_collect.cpp,
// sema_expr.cpp, sema_stmt.cpp, sema_decl.cpp respectively.

#include "sema_impl.hpp"

#include <logos/compiler/sha256.hpp>

#include <cstdio>
#include <format>
#include <functional>

namespace logos::compiler {

namespace la = ast;
using hermes::TinyMapView;
using hermes::ArrayView;
using hermes::StringView;
using hermes::AnyVal;
using hermes::MemHolder;

// ── types_equal ─────────────────────────────────────────────────────────────

bool types_equal(const LogosType& a, const LogosType& b) noexcept {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
    case LogosType::Kind::Ptr:
        return a.mut_ptr == b.mut_ptr &&
               a.pointee && b.pointee &&
               types_equal(*a.pointee, *b.pointee);
    case LogosType::Kind::Ref:
    case LogosType::Kind::MutRef:
        return a.pointee && b.pointee &&
               types_equal(*a.pointee, *b.pointee);
        // Note: we intentionally ignore lifetime in equality — structural type equality
    case LogosType::Kind::Array:
        return a.arr_size == b.arr_size &&
               a.arr_size_var == b.arr_size_var &&
               a.elem && b.elem &&
               types_equal(*a.elem, *b.elem);
    case LogosType::Kind::Struct:
    case LogosType::Kind::Datatype:
        if (a.struct_name != b.struct_name) return false;
        if (a.type_args.size() != b.type_args.size()) return false;
        for (size_t i = 0; i < a.type_args.size(); ++i)
            if (!a.type_args[i] || !b.type_args[i] ||
                !types_equal(*a.type_args[i], *b.type_args[i])) return false;
        return true;
    case LogosType::Kind::Enum:
        return a.enum_name == b.enum_name;
    case LogosType::Kind::Tuple:
        if (a.tuple_elems.size() != b.tuple_elems.size()) return false;
        for (size_t i = 0; i < a.tuple_elems.size(); ++i)
            if (!a.tuple_elems[i] || !b.tuple_elems[i] ||
                !types_equal(*a.tuple_elems[i], *b.tuple_elems[i])) return false;
        return true;
    case LogosType::Kind::Slice:
        return a.elem && b.elem && types_equal(*a.elem, *b.elem);
    case LogosType::Kind::TraitObject:
        return a.trait_name == b.trait_name;
    case LogosType::Kind::ImplTrait:
        return a.struct_name == b.struct_name;
    case LogosType::Kind::AssocType:
        return a.assoc_type_name == b.assoc_type_name &&
               a.trait_name == b.trait_name &&
               a.assoc_base && b.assoc_base &&
               types_equal(*a.assoc_base, *b.assoc_base);
    default:
        return true;  // primitives
    }
}

// ── Generic struct name helpers ───────────────────────────────────────────────

static std::string mangle_type_for_name(const LogosType* t);

std::string concrete_struct_name(const LogosType* t) {
    if (!t || (t->kind != LogosType::Kind::Struct &&
               t->kind != LogosType::Kind::Datatype)) return {};
    if (t->type_args.empty()) return t->struct_name;
    std::string r = t->struct_name + "$G" + std::to_string(t->type_args.size());
    for (auto* a : t->type_args) { r += "$"; r += mangle_type_for_name(a); }
    return r;
}

static std::string mangle_type_for_name(const LogosType* t) {
    if (!t) return "null";
    switch (t->kind) {
    case LogosType::Kind::Ptr:
        return (t->mut_ptr ? "pmut_" : "pcst_") + mangle_type_for_name(t->pointee);
    case LogosType::Kind::Ref:
        return "ref_" + mangle_type_for_name(t->pointee);
    case LogosType::Kind::MutRef:
        return "refmut_" + mangle_type_for_name(t->pointee);
    case LogosType::Kind::Array:
        return "arr" + std::to_string(t->arr_size) + "_" + mangle_type_for_name(t->elem);
    case LogosType::Kind::Struct:
    case LogosType::Kind::Datatype:
        return concrete_struct_name(t);
    case LogosType::Kind::Tuple: {
        std::string r = "tup$" + std::to_string(t->tuple_elems.size());
        for (auto* e : t->tuple_elems) { r += "$"; r += mangle_type_for_name(e); }
        return r;
    }
    case LogosType::Kind::Slice:
        return "slice_" + mangle_type_for_name(t->elem);
    case LogosType::Kind::AssocType:
        return mangle_type_for_name(t->assoc_base) + "::" + t->assoc_type_name;
    default:
        return type_str(t);  // primitives / TypeVar / Enum already valid identifiers
    }
}

std::string SemaChecker::canonical_func_type_name(const LogosType* t) const {
    return mangle_type_for_name(t);
}

std::string SemaChecker::function_signature_key(std::string_view base_name,
                                                const std::vector<const LogosType*>& param_types,
                                                bool is_vararg) const {
    std::string key(base_name);
    for (auto* pt : param_types) {
        key += "__";
        key += canonical_func_type_name(pt);
    }
    if (param_types.empty()) key += "__void";
    if (is_vararg) key += "__vararg";
    return key;
}

std::string SemaChecker::function_symbol_name(std::string_view base_name,
                                             const SemaChecker::SemaFuncInfo& info) const {
    std::string key = function_signature_key(base_name, info.param_types, info.is_vararg);
    auto suffix = key.substr(std::string(base_name).size() + 2);
    return std::string(base_name) + (info.type_params.empty() ? "__f__" : "__g__") + suffix;
}

const SemaChecker::SemaFuncInfo* SemaChecker::find_func_by_symbol(std::string_view symbol) const {
    if (auto it = funcs_.find(std::string(symbol)); it != funcs_.end())
        return &it->second;
    if (auto it = generic_funcs_.find(std::string(symbol)); it != generic_funcs_.end())
        return &it->second;
    return nullptr;
}

const SemaChecker::SemaFuncInfo* SemaChecker::find_generic_func(std::string_view base_name) const {
    if (auto git = generic_overloads_.find(std::string(base_name)); git != generic_overloads_.end()) {
        for (auto& sym : git->second) {
            auto fit = generic_funcs_.find(sym);
            if (fit != generic_funcs_.end())
                return &fit->second;
        }
    }
    return nullptr;
}

const SemaChecker::SemaFuncInfo* SemaChecker::find_generic_func(std::string_view base_name,
                                                                size_t n_args) const {
    const SemaFuncInfo* fallback = nullptr;
    if (auto git = generic_overloads_.find(std::string(base_name)); git != generic_overloads_.end()) {
        for (auto& sym : git->second) {
            auto fit = generic_funcs_.find(sym);
            if (fit == generic_funcs_.end()) continue;
            auto& fi = fit->second;
            if (fi.is_vararg) {
                if (n_args >= fi.param_types.size()) return &fi;
            } else if (fi.param_types.size() == n_args) {
                return &fi;
            } else if (!fallback) {
                fallback = &fi;
            }
        }
    }
    return fallback;
}

const SemaChecker::SemaFuncInfo* SemaChecker::find_func_by_base_and_signature(
        std::string_view base_name,
        const std::vector<const LogosType*>& param_types,
        bool is_vararg) const {
    for (auto* fi : find_func_candidates(base_name)) {
        if (fi->is_vararg != is_vararg) continue;
        if (fi->param_types.size() != param_types.size()) continue;
        bool same = true;
        for (size_t i = 0; i < param_types.size(); ++i) {
            if (!fi->param_types[i] || !param_types[i] ||
                !types_equal(*fi->param_types[i], *param_types[i])) {
                same = false; break;
            }
        }
        if (same) return fi;
    }
    return nullptr;
}

std::vector<const SemaChecker::SemaFuncInfo*> SemaChecker::find_func_candidates(std::string_view base_name) const {
    std::vector<const SemaChecker::SemaFuncInfo*> out;
    if (auto it = func_overloads_.find(std::string(base_name)); it != func_overloads_.end()) {
        for (auto& sym : it->second) {
            auto fit = funcs_.find(sym);
            if (fit != funcs_.end()) out.push_back(&fit->second);
        }
    } else {
        auto fit = funcs_.find(std::string(base_name));
        if (fit != funcs_.end() && fit->second.source_file.size())
            out.push_back(&fit->second);
    }
    if (auto git = generic_overloads_.find(std::string(base_name)); git != generic_overloads_.end()) {
        for (auto& sym : git->second) {
            auto fit = generic_funcs_.find(sym);
            if (fit != generic_funcs_.end()) out.push_back(&fit->second);
        }
    }
    return out;
}

const SemaChecker::SemaFuncInfo* SemaChecker::resolve_function_call(
        std::string_view base_name,
        const std::vector<lir::LExprPtr>& arg_exprs,
        bool allow_generic,
        bool exact_only) const {
    const SemaChecker::SemaFuncInfo* best = nullptr;
    int best_score = -1;
    bool ambiguous = false;

    auto candidates = find_func_candidates(base_name);
    for (auto* fi : candidates) {
        if (!fi || fi->type_params.size() || fi->source_file.empty()) continue;
        bool arity_ok = fi->is_vararg ? arg_exprs.size() >= fi->param_types.size()
                                      : arg_exprs.size() == fi->param_types.size();
        if (!arity_ok) continue;

        int score = 0;
        bool ok = true;
        for (size_t i = 0; i < fi->param_types.size(); ++i) {
            auto* at = arg_exprs[i] ? arg_exprs[i]->type : nullptr;
            auto* pt = fi->param_types[i];
            if (!at || !pt) { ok = false; break; }
            if (types_equal(*at, *pt)) score = std::max(score, 2);
            else if (!exact_only && types_compatible(at, pt)) score = std::max(score, 1);
            else { ok = false; break; }
        }
        if (!ok) continue;
        if (score > best_score) {
            best = fi;
            best_score = score;
            ambiguous = false;
        } else if (score == best_score && best_score != -1) {
            ambiguous = true;
        }
    }

    if (ambiguous) {
        const_cast<SemaChecker*>(this)->error(std::format("ambiguous call to '{}'", base_name));
        return nullptr;
    }
    if (best || !allow_generic) return best;
    return nullptr;
}

// ── types_compatible ─────────────────────────────────────────────────────────

bool types_compatible(const LogosType* from, const LogosType* to) noexcept {
    if (!from || !to) return false;
    if (types_equal(*from, *to)) return true;
    if (from->kind == LogosType::Kind::IntLit && is_integer_kind(to->kind)) return true;
    if (from->kind == LogosType::Kind::IntLit && to->kind == LogosType::Kind::TypeVar) return true;
    if (from->kind == LogosType::Kind::IntLit &&
        (to->kind == LogosType::Kind::F32 || to->kind == LogosType::Kind::F64)) return true;
    if (from->kind == LogosType::Kind::FloatLit &&
        (to->kind == LogosType::Kind::F32 || to->kind == LogosType::Kind::F64 ||
         to->kind == LogosType::Kind::TypeVar)) return true;
    if (from->kind == LogosType::Kind::Enum   && is_integer_kind(to->kind)) return true;
    if (is_integer_kind(from->kind) && to->kind == LogosType::Kind::Enum)   return true;
    // Safe implicit integer widening (e.g. u32 → i64, i32 → i64, u8 → u32).
    // Value preservation guaranteed; signed→unsigned never allowed here.
    if (can_widen_int(from->kind, to->kind)) return true;
    if (from->kind == LogosType::Kind::Array &&
        to->kind   == LogosType::Kind::Ptr   &&
        from->elem && to->pointee)
        return types_equal(*from->elem, *to->pointee);
    // Arrays are compatible if same size and elements are compatible (handles nested arrays).
    if (from->kind == LogosType::Kind::Array && to->kind == LogosType::Kind::Array &&
        from->arr_size == to->arr_size && from->elem && to->elem)
        return types_compatible(from->elem, to->elem);
    // Tuple: element-wise compatibility (e.g. ({integer}, {integer}) → (i32, i32))
    if (from->kind == LogosType::Kind::Tuple && to->kind == LogosType::Kind::Tuple) {
        if (from->tuple_elems.size() != to->tuple_elems.size()) return false;
        for (size_t i = 0; i < from->tuple_elems.size(); ++i)
            if (!types_compatible(from->tuple_elems[i], to->tuple_elems[i])) return false;
        return true;
    }
    // Struct → &dyn Trait coercion (impl check deferred to codegen)
    if (to->kind == LogosType::Kind::TraitObject &&
        (from->kind == LogosType::Kind::Struct ||
         (from->kind == LogosType::Kind::Ptr && from->pointee)))
        return true;
    // &T / &mut T → *const T / *mut T coercions (for backward compat with existing raw-ptr code)
    if ((from->kind == LogosType::Kind::Ref || from->kind == LogosType::Kind::MutRef) &&
        to->kind == LogosType::Kind::Ptr &&
        from->pointee && to->pointee)
        return types_compatible(from->pointee, to->pointee);
    // *const T / *mut T → &T (reverse coercion — less safe but needed for existing code)
    if (from->kind == LogosType::Kind::Ptr &&
        (to->kind == LogosType::Kind::Ref || to->kind == LogosType::Kind::MutRef) &&
        from->pointee && to->pointee)
        return types_compatible(from->pointee, to->pointee);
    // &mut T → &T coercion (shared ref from exclusive ref)
    if (from->kind == LogosType::Kind::MutRef && to->kind == LogosType::Kind::Ref &&
        from->pointee && to->pointee)
        return types_compatible(from->pointee, to->pointee);
    // &T → &T and &mut T → &mut T with compatible pointees (e.g. &{integer} → &i32)
    if (from->kind == LogosType::Kind::Ref && to->kind == LogosType::Kind::Ref &&
        from->pointee && to->pointee)
        return types_compatible(from->pointee, to->pointee);
    if (from->kind == LogosType::Kind::MutRef && to->kind == LogosType::Kind::MutRef &&
        from->pointee && to->pointee)
        return types_compatible(from->pointee, to->pointee);
    // *mut T → *const T coercion (dropping write permission is always safe).
    if (from->kind == LogosType::Kind::Ptr && to->kind == LogosType::Kind::Ptr &&
        from->mut_ptr && !to->mut_ptr &&
        from->pointee && to->pointee)
        return types_compatible(from->pointee, to->pointee);
    // *const u8 (or any *T) → &tagged<TS> Trait coercion.
    // &tagged<TS> Trait is a thin pointer to a tagged object.  The caller passes
    // a raw *const u8 and the compiler reads the tag at dispatch time.
    if (to->kind == LogosType::Kind::TaggedPtr && from->kind == LogosType::Kind::Ptr)
        return true;
    // Class pointer covariance: *mut/const Derived is compatible with *const/mut Class
    // (same class — exact equality handled above; hierarchy checked in SemaChecker::compat)
    return false;
}

// ── type_str ─────────────────────────────────────────────────────────────────

std::string type_str(const LogosType* t) {
    if (!t) return "<null>";
    switch (t->kind) {
    case LogosType::Kind::Void:   return "void";
    case LogosType::Kind::I32:    return "i32";
    case LogosType::Kind::I64:    return "i64";
    case LogosType::Kind::F64:    return "f64";
    case LogosType::Kind::F32:    return "f32";
    case LogosType::Kind::Bool:   return "bool";
    case LogosType::Kind::U8:     return "u8";
    case LogosType::Kind::I8:     return "i8";
    case LogosType::Kind::I16:    return "i16";
    case LogosType::Kind::U16:    return "u16";
    case LogosType::Kind::I24:    return "i24";
    case LogosType::Kind::U24:    return "u24";
    case LogosType::Kind::I56:    return "i56";
    case LogosType::Kind::U56:    return "u56";
    case LogosType::Kind::U32:    return "u32";
    case LogosType::Kind::U64:    return "u64";
    case LogosType::Kind::I128:   return "i128";
    case LogosType::Kind::U128:   return "u128";
    case LogosType::Kind::IntLit:   return "{integer}";
    case LogosType::Kind::FloatLit: return "{float}";
    case LogosType::Kind::Ptr:
        return std::string(t->mut_ptr ? "*mut " : "*const ") + type_str(t->pointee);
    case LogosType::Kind::Ref: {
        std::string s = "&";
        if (!t->lifetime.empty()) s += t->lifetime + " ";
        return s + type_str(t->pointee);
    }
    case LogosType::Kind::MutRef: {
        std::string s = "&";
        if (!t->lifetime.empty()) s += t->lifetime + " ";
        return s + "mut " + type_str(t->pointee);
    }
    case LogosType::Kind::Array:
        return std::format("[{}; {}]", type_str(t->elem), t->arr_size);
    case LogosType::Kind::Struct:
    case LogosType::Kind::Datatype:
        if (t->type_args.empty() && t->lifetime_args.empty()) return t->struct_name;
        { std::string r = t->struct_name + "<";
          bool first = true;
          for (auto& lt : t->lifetime_args) {
              if (!first) r += ", "; first = false;
              r += lt;
          }
          for (size_t i = 0; i < t->type_args.size(); ++i) {
              if (!first) r += ", "; first = false;
              r += type_str(t->type_args[i]);
          }
          return r + ">"; }
    case LogosType::Kind::Tuple: {
        std::string r = "(";
        for (size_t i = 0; i < t->tuple_elems.size(); ++i) {
            if (i) r += ", ";
            r += type_str(t->tuple_elems[i]);
        }
        return r + ")"; }
    case LogosType::Kind::Slice:
        return std::format("&[{}]", type_str(t->elem));
    case LogosType::Kind::Closure: {
        std::string r = "|";
        for (size_t i = 0; i < t->closure_params.size(); ++i) {
            if (i) r += ", ";
            r += type_str(t->closure_params[i]);
        }
        r += "| -> ";
        r += type_str(t->closure_ret);
        return r; }
    case LogosType::Kind::FnPtr: {
        std::string r = "fn(";
        for (size_t i = 0; i < t->closure_params.size(); ++i) {
            if (i) r += ", ";
            r += type_str(t->closure_params[i]);
        }
        r += ") -> ";
        r += type_str(t->closure_ret);
        return r; }
    case LogosType::Kind::Enum:        return t->enum_name;
    case LogosType::Kind::TraitObject: return "&dyn " + t->trait_name;
    case LogosType::Kind::TaggedPtr:   return "&tagged<" + t->struct_name + "> " + t->trait_name;
    case LogosType::Kind::TypeVar:     return std::string(t->type_var_name);
    case LogosType::Kind::ConstVar:    return std::string(t->type_var_name);
    case LogosType::Kind::AssocType: {
        std::string r = type_str(t->assoc_base) + "::" + t->assoc_type_name;
        if (!t->gat_args.empty()) {
            r += "<";
            for (size_t i = 0; i < t->gat_args.size(); ++i) {
                if (i) r += ", ";
                r += type_str(t->gat_args[i]);
            }
            r += ">";
        }
        return r;
    }
    case LogosType::Kind::ImplTrait:   return "impl " + t->struct_name;
    case LogosType::Kind::Error:   return "<error>";
    }
    return "<unknown>";
}

// ── SemaChecker method definitions ───────────────────────────────────────────

lir::LProgram SemaChecker::run(const std::vector<hermes::Hermes>& asts,
                                const std::vector<std::string>& filenames) {
    filenames_ = &filenames;
    init_primitives();
    collect(asts);

    lir::LProgram prog;
    if (!result_.ok()) {
        prog.diags = std::move(result_);
        prog.type_pool = std::move(pool_);
        return prog;
    }

    lower_program(asts, prog);

    // Enforce the "one eidos per (genos, tag-system)" invariant at compile
    // time.  Two different impl targets that end up with the same
    // (tag_system, type_code) pair would overwrite each other in the
    // dispatch table (and trigger a link-time collision via the sentinel
    // globals in mlir_gen_dyn.cpp).  Surface a clearer diagnostic here.
    {
        std::unordered_map<std::string, std::string> seen;
        for (const auto& de : prog.dispatch_entries) {
            if (de.type_code == 0) continue;
            auto key = de.tag_system + "#" + std::to_string(de.type_code);
            auto [it, inserted] = seen.emplace(std::move(key), de.impl_type_name);
            if (!inserted && it->second != de.impl_type_name) {
                ctx_.clear();
                error(std::format(
                    "two eide register for genos type_code {} in tag system '{}': "
                    "'{}' and '{}' — only one eidos per (genos, tag-system) is allowed",
                    de.type_code, de.tag_system,
                    it->second, de.impl_type_name));
            }
        }
    }

    prog.diags      = std::move(result_);
    prog.type_pool  = std::move(pool_);
    return prog;
}

void SemaChecker::init_primitives() {
    auto ap = [&](LogosType::Kind k) {
        LogosType t; t.kind = k;
        prims_[int(k)] = pool_.alloc(t);
    };
    ap(LogosType::Kind::Void);
    ap(LogosType::Kind::I32);
    ap(LogosType::Kind::I64);
    ap(LogosType::Kind::F64);
    ap(LogosType::Kind::F32);
    ap(LogosType::Kind::Bool);
    ap(LogosType::Kind::U8);
    ap(LogosType::Kind::I8);
    ap(LogosType::Kind::I16);
    ap(LogosType::Kind::U16);
    ap(LogosType::Kind::U32);
    ap(LogosType::Kind::U64);
    ap(LogosType::Kind::I24);
    ap(LogosType::Kind::U24);
    ap(LogosType::Kind::I24);
    ap(LogosType::Kind::I56);
    ap(LogosType::Kind::U24);
    ap(LogosType::Kind::U56);
    ap(LogosType::Kind::I128);
    ap(LogosType::Kind::U128);
    ap(LogosType::Kind::IntLit);
    ap(LogosType::Kind::FloatLit);
    ap(LogosType::Kind::Error);
}

const LogosType* SemaChecker::lookup_type_by_name(std::string_view name) {
    if (name == "i32")  return prim(LogosType::Kind::I32);
    if (name == "i64")  return prim(LogosType::Kind::I64);
    if (name == "f64")  return prim(LogosType::Kind::F64);
    if (name == "f32")  return prim(LogosType::Kind::F32);
    if (name == "bool") return prim(LogosType::Kind::Bool);
    if (name == "u8")   return prim(LogosType::Kind::U8);
    if (name == "i8")   return prim(LogosType::Kind::I8);
    if (name == "i16")  return prim(LogosType::Kind::I16);
    if (name == "u16")  return prim(LogosType::Kind::U16);
    if (name == "u32")  return prim(LogosType::Kind::U32);
    if (name == "u64")  return prim(LogosType::Kind::U64);
    if (name == "i24")  return prim(LogosType::Kind::I24);
    if (name == "u24")  return prim(LogosType::Kind::U24);
    if (name == "i56")  return prim(LogosType::Kind::I56);
    if (name == "u56")  return prim(LogosType::Kind::U56);
    if (name == "i128") return prim(LogosType::Kind::I128);
    if (name == "u128") return prim(LogosType::Kind::U128);
    if (name == "usize") return prim(LogosType::Kind::U64);  // usize = u64
    if (name == "isize") return prim(LogosType::Kind::I64);  // isize = i64
    if (name == "void") return prim(LogosType::Kind::Void);
    if (name == "str")  return make_slice_type(u8_t());
    auto tvit = current_type_params_.find(std::string(name));
    if (tvit != current_type_params_.end()) return tvit->second;
    auto ait = type_aliases_.find(std::string(name));
    if (ait != type_aliases_.end() &&
        ait->second.type_params.empty() && ait->second.lifetime_params.empty())
        return ait->second.type;
    if (structs_.count(std::string(name))) return make_struct_type(name);
    if (datatypes_.count(std::string(name))) return make_datatype_type(name);
    if (enums_.count(std::string(name)))   return make_enum_type(name);
    return nullptr;
}

// ── Drop/move helpers ────────────────────────────────────────────────────────

bool SemaChecker::is_move_type(const LogosType* t) const {
    if (!t) return false;
    // Struct types are move types unless they implement Copy.
    if (t->kind != LogosType::Kind::Struct)
        return false;
    return !copy_types_.count(t->struct_name);
}

std::string SemaChecker::drop_fn_for(const LogosType* t) const {
    if (!t) return {};
    std::string type_name;
    if (t->kind == LogosType::Kind::Struct) type_name = t->struct_name;
    if (type_name.empty()) return {};
    std::string mangled = type_name + "__drop";
    std::vector<const LogosType*> sig{t};
    if (auto* fi = find_func_by_base_and_signature(mangled, sig, false))
        return fi->symbol_name.empty() ? mangled : fi->symbol_name;
    for (auto* cand : find_func_candidates(mangled)) {
        if (!cand || cand->param_types.size() != 1) continue;
        auto* pt = cand->param_types[0];
        if (pt && types_equal(*pt, *t))
            return cand->symbol_name.empty() ? mangled : cand->symbol_name;
    }
    return {};
}

bool SemaChecker::has_droppable_fields(const LogosType* t) const {
    if (!t || t->kind != LogosType::Kind::Struct) return false;
    auto sit = structs_.find(t->struct_name);
    if (sit == structs_.end()) return false;
    for (auto& f : sit->second.fields) {
        if (!drop_fn_for(f.type).empty()) return true;
        if (has_droppable_fields(f.type)) return true;
    }
    return false;
}

std::optional<lir::LStmt> SemaChecker::make_drop_stmt(const std::string& name, const VarInfo& info) const {
    auto dfn = drop_fn_for(info.type);
    bool df  = has_droppable_fields(info.type);
    if (dfn.empty() && !df) return std::nullopt;
    return lir::LStmt{node_line_, lir::SDrop{name, dfn, info.type, df}};
}

std::vector<lir::LStmt> SemaChecker::collect_drops() const {
    std::vector<lir::LStmt> drops;
    if (scope_.empty()) return drops;
    auto& frame = scope_.back();
    for (auto it = frame.var_order.rbegin(); it != frame.var_order.rend(); ++it) {
        if (moved_vars_.count(*it)) continue;
        auto vit = frame.vars.find(*it);
        if (vit == frame.vars.end()) continue;
        if (auto d = make_drop_stmt(*it, vit->second))
            drops.push_back(std::move(*d));
    }
    return drops;
}

std::vector<lir::LStmt> SemaChecker::collect_all_drops() const {
    std::vector<lir::LStmt> drops;
    for (auto fit = scope_.rbegin(); fit != scope_.rend(); ++fit) {
        for (auto it = fit->var_order.rbegin(); it != fit->var_order.rend(); ++it) {
            if (moved_vars_.count(*it)) continue;
            auto vit = fit->vars.find(*it);
            if (vit == fit->vars.end()) continue;
            if (auto d = make_drop_stmt(*it, vit->second))
                drops.push_back(std::move(*d));
        }
    }
    return drops;
}

// ── Name helpers ─────────────────────────────────────────────────────────────

std::string_view SemaChecker::struct_name_of(std::string_view var_name) {
    auto* t = lookup(var_name);
    if (!t) return {};
    if (t->kind == LogosType::Kind::Struct ||
        t->kind == LogosType::Kind::Datatype) return t->struct_name;
    if (is_ref_like(t->kind) && t->pointee &&
        (t->pointee->kind == LogosType::Kind::Struct ||
         t->pointee->kind == LogosType::Kind::Datatype))
        return t->pointee->struct_name;
    return {};
}


std::string_view SemaChecker::struct_name_from_type(const LogosType* t) {
    if (!t) return {};
    if (t->kind == LogosType::Kind::Struct || t->kind == LogosType::Kind::Datatype) {
        // For generic instantiations, the method is registered under the mangled name.
        if (!t->type_args.empty()) {
            // Store in a thread-local to return a stable string_view.
            thread_local std::string buf;
            buf = concrete_struct_name(t);
            return buf;
        }
        return t->struct_name;
    }
    if (is_ref_like(t->kind) && t->pointee &&
        (t->pointee->kind == LogosType::Kind::Struct ||
         t->pointee->kind == LogosType::Kind::Datatype)) {
        if (!t->pointee->type_args.empty()) {
            thread_local std::string buf2;
            buf2 = concrete_struct_name(t->pointee);
            return buf2;
        }
        return t->pointee->struct_name;
    }
    return {};
}


// ── Type parameter helpers ───────────────────────────────────────────────────

std::vector<std::string> SemaChecker::read_lifetime_params(TinyMapView node) {
    std::vector<std::string> result;
    if (!node.has_key(la::TYPE_PARAMS)) return result;
    AnyVal tpav = node.get(la::TYPE_PARAMS.code);
    if (tpav.is_null()) return result;
    auto tplist = map_of(tpav);
    if (!tplist.has_key(la::ITEMS)) return result;
    auto tpitems = arr_of(tplist.get(la::ITEMS.code));
    for (uint64_t i = 0; i < tpitems.size(); ++i) {
        auto tpnode = map_of(tpitems.get(i));
        if (code_of(tpnode) != la::LIFETIME_PARAM) continue;
        result.push_back(std::string(str_of(tpnode.get(la::NAME.code))));
    }
    return result;
}

std::vector<TypeParam> SemaChecker::read_type_params_from(TinyMapView node, int32_t field_code) {
    std::vector<TypeParam> result;
    AnyVal tpav = node.get(field_code);
    if (tpav.is_null()) return result;
    auto tplist = map_of(tpav);
    if (!tplist.has_key(la::ITEMS)) return result;
    auto tpitems = arr_of(tplist.get(la::ITEMS.code));
    // Pre-pass: add all type param names as typevars so bounds referencing sibling params resolve.
    std::vector<std::string> temp_params;
    for (uint64_t i = 0; i < tpitems.size(); ++i) {
        auto tpnode = map_of(tpitems.get(i));
        if (code_of(tpnode) == la::TYPE_PARAM) {
            auto name = std::string(str_of(tpnode.get(la::NAME.code)));
            if (!current_type_params_.count(name)) {
                current_type_params_[name] = make_typevar(name);
                temp_params.push_back(name);
            }
        }
    }
    for (uint64_t i = 0; i < tpitems.size(); ++i) {
        auto tpnode = map_of(tpitems.get(i));
        if (code_of(tpnode) == la::LIFETIME_PARAM) continue;
        if (code_of(tpnode) == la::CONST_PARAM) {
            TypeParam tp;
            tp.name = std::string(str_of(tpnode.get(la::NAME.code)));
            tp.is_const = true;
            tp.const_type = resolve_type(map_of(tpnode.get(la::TYPE.code)));
            result.push_back(std::move(tp));
            continue;
        }
        if (code_of(tpnode) != la::TYPE_PARAM) continue;
        TypeParam tp;
        tp.name = std::string(str_of(tpnode.get(la::NAME.code)));
        if (tpnode.has_key(la::IS_VARIADIC)) {
            AnyVal av = tpnode.get(la::IS_VARIADIC.code);
            tp.is_variadic = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
        }
        if (tpnode.has_key(la::ITEMS)) {
            auto bounds = arr_of(tpnode.get(la::ITEMS.code));
            for (uint64_t b = 0; b < bounds.size(); ++b) {
                auto bnode = map_of(bounds.get(b));
                if (code_of(bnode) == la::TRAIT_BOUND) {
                    TraitBound tb;
                    tb.trait_name = std::string(str_of(bnode.get(la::NAME.code)));
                    // Parse type args: Trait<i32, T> -> [i32, T]
                    if (bnode.has_key(la::TYPE_PARAMS)) {
                        auto tpav2 = bnode.get(la::TYPE_PARAMS.code);
                        if (!tpav2.is_null()) {
                            auto tamap = map_of(tpav2);
                            if (tamap.has_key(la::ITEMS)) {
                                auto taitems = arr_of(tamap.get(la::ITEMS.code));
                                for (uint64_t ti = 0; ti < taitems.size(); ++ti)
                                    tb.type_args.push_back(
                                        resolve_type(map_of(taitems.get(ti))));
                            }
                        }
                    }
                    tp.bounds.push_back(std::move(tb));
                }
            }
        }
        result.push_back(std::move(tp));
    }
    // Remove temp typevars added in pre-pass (push_type_params will re-add them properly).
    for (auto& name : temp_params)
        current_type_params_.erase(name);
    return result;
}

std::vector<TypeParam> SemaChecker::read_type_params(TinyMapView node) {
    std::vector<TypeParam> result;
    if (!node.has_key(la::TYPE_PARAMS)) return result;
    AnyVal tpav = node.get(la::TYPE_PARAMS.code);
    if (tpav.is_null()) return result;
    // type_param_list => { ITEMS: $... }
    auto tplist = map_of(tpav);
    if (!tplist.has_key(la::ITEMS)) return result;
    auto tpitems = arr_of(tplist.get(la::ITEMS.code));
    // Pre-pass: add all type param names as typevars so bounds referencing sibling params resolve.
    std::vector<std::string> temp_params;
    for (uint64_t i = 0; i < tpitems.size(); ++i) {
        auto tpnode = map_of(tpitems.get(i));
        if (code_of(tpnode) == la::TYPE_PARAM) {
            auto name = std::string(str_of(tpnode.get(la::NAME.code)));
            if (!current_type_params_.count(name)) {
                current_type_params_[name] = make_typevar(name);
                temp_params.push_back(name);
            }
        }
    }
    for (uint64_t i = 0; i < tpitems.size(); ++i) {
        auto tpnode = map_of(tpitems.get(i));
        // Skip lifetime params ('a) — deferred to borrow checker.
        if (code_of(tpnode) == la::LIFETIME_PARAM) continue;
        if (code_of(tpnode) == la::CONST_PARAM) {
            TypeParam tp;
            tp.name = std::string(str_of(tpnode.get(la::NAME.code)));
            tp.is_const = true;
            tp.const_type = resolve_type(map_of(tpnode.get(la::TYPE.code)));
            result.push_back(std::move(tp));
            continue;
        }
        if (code_of(tpnode) != la::TYPE_PARAM) continue;
        TypeParam tp;
        tp.name = std::string(str_of(tpnode.get(la::NAME.code)));
        // Check variadic flag (T...)
        if (tpnode.has_key(la::IS_VARIADIC)) {
            AnyVal av = tpnode.get(la::IS_VARIADIC.code);
            tp.is_variadic = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
        }
        // Optional bounds: ITEMS contains TRAIT_BOUND nodes
        if (tpnode.has_key(la::ITEMS)) {
            auto bounds = arr_of(tpnode.get(la::ITEMS.code));
            for (uint64_t b = 0; b < bounds.size(); ++b) {
                auto bnode = map_of(bounds.get(b));
                if (code_of(bnode) == la::TRAIT_BOUND) {
                    TraitBound tb;
                    tb.trait_name = std::string(str_of(bnode.get(la::NAME.code)));
                    // Parse type args: Trait<i32, T> -> [i32, T]
                    if (bnode.has_key(la::TYPE_PARAMS)) {
                        auto tpav2 = bnode.get(la::TYPE_PARAMS.code);
                        if (!tpav2.is_null()) {
                            auto tamap = map_of(tpav2);
                            if (tamap.has_key(la::ITEMS)) {
                                auto taitems = arr_of(tamap.get(la::ITEMS.code));
                                for (uint64_t ti = 0; ti < taitems.size(); ++ti)
                                    tb.type_args.push_back(
                                        resolve_type(map_of(taitems.get(ti))));
                            }
                        }
                    }
                    tp.bounds.push_back(std::move(tb));
                }
            }
        }
        // Validate: variadic param must be last
        if (tp.is_variadic && i + 1 < tpitems.size())
            error("variadic type parameter must be last in the type parameter list");
        result.push_back(std::move(tp));
    }
    // Remove temp typevars added in pre-pass.
    for (auto& name : temp_params)
        current_type_params_.erase(name);
    // Merge bounds from `where T: Trait, U: Trait2` clause.
    if (node.has_key(la::WHERE)) {
        AnyVal wav = node.get(la::WHERE.code);
        if (!wav.is_null()) {
            auto wnode = map_of(wav);
            if (wnode.has_key(la::ITEMS)) {
                auto witems = arr_of(wnode.get(la::ITEMS.code));
                for (uint64_t i = 0; i < witems.size(); ++i) {
                    auto constraint = map_of(witems.get(i));
                    if (code_of(constraint) != la::TYPE_PARAM) continue;
                    auto tname = std::string(str_of(constraint.get(la::NAME.code)));
                    // Find the type param in result and add bounds.
                    TypeParam* tp_ptr = nullptr;
                    for (auto& tp : result)
                        if (tp.name == tname) { tp_ptr = &tp; break; }
                    if (!tp_ptr) {
                        // type param in where clause not in param list — add it
                        TypeParam tp; tp.name = tname;
                        result.push_back(std::move(tp));
                        tp_ptr = &result.back();
                    }
                    if (constraint.has_key(la::ITEMS)) {
                        auto bounds = arr_of(constraint.get(la::ITEMS.code));
                        for (uint64_t b = 0; b < bounds.size(); ++b) {
                            auto bnode = map_of(bounds.get(b));
                            if (code_of(bnode) == la::TRAIT_BOUND) {
                                TraitBound tb;
                                tb.trait_name = std::string(str_of(bnode.get(la::NAME.code)));
                                tp_ptr->bounds.push_back(std::move(tb));
                            }
                        }
                    }
                }
            }
        }
    }
    return result;
}

// ── Sema-side type substitution ──────────────────────────────────────────────

const LogosType* SemaChecker::subst_type_sema(const LogosType* t, const SemaSubst& s,
                                               const SemaLifetimeSubst& ls) {
    if (!t) return t;
    switch (t->kind) {
    case LogosType::Kind::ConstVar:
    case LogosType::Kind::TypeVar: {
        auto it = s.find(t->type_var_name);
        return (it != s.end()) ? it->second : t;
    }
    case LogosType::Kind::Array: {
        auto* elem = subst_type_sema(t->elem, s, ls);
        uint64_t size = t->arr_size;
        std::string symbolic = t->arr_size_var;
        if (!symbolic.empty()) {
            auto it = s.find(symbolic);
            if (it != s.end()) {
                if (it->second->const_val) {
                    size = (uint64_t)*it->second->const_val;
                    symbolic = "";
                } else if (it->second->kind == LogosType::Kind::ConstVar) {
                    symbolic = it->second->type_var_name;
                }
            }
        }
        if (elem == t->elem && size == t->arr_size && symbolic == t->arr_size_var) return t;
        return make_array(elem, size, symbolic);
    }
    case LogosType::Kind::Ptr: {
        auto* inner = subst_type_sema(t->pointee, s, ls);
        if (inner == t->pointee) return t;
        return make_ptr(t->mut_ptr, inner);
    }
    case LogosType::Kind::Ref:
    case LogosType::Kind::MutRef: {
        auto* inner = subst_type_sema(t->pointee, s, ls);
        std::string lt = t->lifetime;
        if (!lt.empty()) { auto it = ls.find(lt); if (it != ls.end()) lt = it->second; }
        if (inner == t->pointee && lt == t->lifetime) return t;
        return make_ref(t->kind == LogosType::Kind::MutRef, inner, lt);
    }
    case LogosType::Kind::Struct:
    case LogosType::Kind::Datatype: {
        if (t->type_args.empty() && t->lifetime_args.empty()) return t;
        std::vector<const LogosType*> new_args;
        bool changed = false;
        for (auto* a : t->type_args) {
            auto* na = subst_type_sema(a, s, ls);
            changed |= (na != a);
            new_args.push_back(na);
        }
        std::vector<std::string> new_lt_args;
        bool lt_changed = false;
        for (auto& lt : t->lifetime_args) {
            auto it = ls.find(lt);
            if (it != ls.end()) { new_lt_args.push_back(it->second); lt_changed = true; }
            else                  new_lt_args.push_back(lt);
        }
        if (!changed && !lt_changed) return t;
        LogosType nt;
        nt.kind = t->kind;
        nt.struct_name = t->struct_name;
        nt.type_args = std::move(new_args);
        nt.lifetime_args = std::move(new_lt_args);
        return pool_.alloc(std::move(nt));
    }
    case LogosType::Kind::Enum: {
        if (t->type_args.empty() && t->lifetime_args.empty()) return t;
        std::vector<const LogosType*> new_args;
        bool changed = false;
        for (auto* a : t->type_args) {
            auto* na = subst_type_sema(a, s, ls);
            changed |= (na != a);
            new_args.push_back(na);
        }
        std::vector<std::string> new_lt_args;
        bool lt_changed = false;
        for (auto& lt : t->lifetime_args) {
            auto it = ls.find(lt);
            if (it != ls.end()) { new_lt_args.push_back(it->second); lt_changed = true; }
            else                  new_lt_args.push_back(lt);
        }
        if (!changed && !lt_changed) return t;
        LogosType nt;
        nt.kind = LogosType::Kind::Enum;
        nt.enum_name = t->enum_name;
        nt.type_args = std::move(new_args);
        nt.lifetime_args = std::move(new_lt_args);
        return pool_.alloc(std::move(nt));
    }
    case LogosType::Kind::Tuple: {
        std::vector<const LogosType*> new_elems;
        bool changed = false;
        for (auto* e : t->tuple_elems) {
            auto* ne = subst_type_sema(e, s, ls);
            changed |= (ne != e);
            new_elems.push_back(ne);
        }
        if (!changed) return t;
        return make_tuple_type(std::move(new_elems));
    }
    case LogosType::Kind::Slice: {
        auto* elem = subst_type_sema(t->elem, s, ls);
        if (elem == t->elem) return t;
        return make_slice_type(elem);
    }
    case LogosType::Kind::Closure:
    case LogosType::Kind::FnPtr: {
        std::vector<const LogosType*> new_params;
        bool changed = false;
        for (auto* p : t->closure_params) {
            auto* np = subst_type_sema(p, s, ls);
            changed |= (np != p);
            new_params.push_back(np);
        }
        auto* new_ret = subst_type_sema(t->closure_ret, s, ls);
        changed |= (new_ret != t->closure_ret);
        if (!changed) return t;

        LogosType nt;
        nt.kind = t->kind;  // preserve Closure vs FnPtr
        nt.closure_params = std::move(new_params);
        nt.closure_ret = new_ret;
        return pool_.alloc(std::move(nt));
    }
    case LogosType::Kind::AssocType: {
        // Substitute the base type first.
        auto* subbed_base = subst_type_sema(t->assoc_base, s, ls);
        
        // Try resolving: if base is substituted to a concrete type, look up impl.
        const LogosType* concrete = nullptr;
        if (subbed_base && subbed_base->kind != LogosType::Kind::TypeVar && subbed_base->kind != LogosType::Kind::ConstVar) {
            concrete = subbed_base;
        } else if (subbed_base && subbed_base->kind == LogosType::Kind::TypeVar) {
             // Even if it's a typevar, perhaps it is a known concrete name like "i32" (though unlikely for TypeVar)
             // actually if it's still a typevar, we can't resolve it yet.
        }
        // If still unresolved, try looking up the *substituted* base typevar by name.
        // Using the original base (often "Self") here can over-resolve in generic contexts
        // where Self was already substituted to another typevar (e.g. T).
        if (!concrete && subbed_base && subbed_base->kind == LogosType::Kind::TypeVar) {
            if (auto* looked = const_cast<SemaChecker*>(this)->lookup_type_by_name(subbed_base->type_var_name)) {
                if (looked->kind != LogosType::Kind::TypeVar &&
                    looked->kind != LogosType::Kind::ConstVar) {
                    concrete = looked;
                }
            }
        }

        // Substitute gat_args as well
        std::vector<const LogosType*> subbed_gat_args;
        bool gat_changed = false;
        for (auto* ga : t->gat_args) {
            auto* nga = subst_type_sema(ga, s, ls);
            gat_changed |= (nga != ga);
            subbed_gat_args.push_back(nga);
        }

        if (concrete) {
            std::string concrete_name = type_str(concrete);
            // Helper: build combined substitution (impl params + GAT params)
            auto make_subst = [&](const AssocTypeEntry& entry) -> SemaSubst {
                SemaSubst combined;
                for (size_t i = 0; i < entry.impl_type_params.size() &&
                                   i < concrete->type_args.size(); ++i)
                    combined[entry.impl_type_params[i].name] = concrete->type_args[i];
                for (size_t i = 0; i < entry.gat_type_params.size() &&
                                   i < subbed_gat_args.size(); ++i)
                    combined[entry.gat_type_params[i].name] = subbed_gat_args[i];
                return combined;
            };

            // 1. Direct lookup (non-generic impls: key stored under concrete name).
            std::string key = t->trait_name + "::" + concrete_name + "::" + t->assoc_type_name;
            auto ait = assoc_type_impls_.find(key);
            if (ait != assoc_type_impls_.end()) {
                return subst_type_sema(ait->second.type, make_subst(ait->second));
            }
            // 2. Base-name fallback (generic impls).
            std::string base_name = (concrete->kind == LogosType::Kind::Struct)
                                    ? concrete->struct_name : "";
            if (!base_name.empty() && base_name != concrete_name) {
                std::string base_key = t->trait_name + "::" + base_name
                                      + "::" + t->assoc_type_name;
                auto ait2 = assoc_type_impls_.find(base_key);
                if (ait2 != assoc_type_impls_.end())
                    return subst_type_sema(ait2->second.type, make_subst(ait2->second));
            }
            // 3. Blanket-impl fallback: `impl<T: Bound> Trait for T` provides
            // `type Assoc = …`.  Use it when `concrete` satisfies Bound.
            for (auto& bi : blanket_impls_) {
                if (bi.trait_name != t->trait_name) continue;
                // Concrete type must implement the blanket's bound trait.
                auto bkey = bi.bound_trait + "::" + concrete_name;
                bool satisfied = impls_.count(bkey) > 0;
                if (!satisfied && !base_name.empty() && base_name != concrete_name) {
                    auto bkey2 = bi.bound_trait + "::" + base_name;
                    satisfied = impls_.count(bkey2) > 0;
                }
                if (!satisfied) continue;
                std::string blanket_key = t->trait_name + "::$blanket$"
                    + t->trait_name + "$" + bi.bound_trait
                    + "$" + bi.target_typevar
                    + "::" + t->assoc_type_name;
                auto bait = assoc_type_impls_.find(blanket_key);
                if (bait == assoc_type_impls_.end()) continue;
                // Substitute the blanket's target typevar → concrete.
                SemaSubst bsubst;
                bsubst[bi.target_typevar] = concrete;
                return subst_type_sema(bait->second.type, bsubst);
            }
        }
        if (subbed_base != t->assoc_base || gat_changed) {
            LogosType nt = *t;
            nt.assoc_base = subbed_base;
            nt.gat_args   = std::move(subbed_gat_args);
            return pool_.alloc(std::move(nt));
        }
        return t;
    }
    default: return t;
    }
}

// ── Type resolution ──────────────────────────────────────────────────────────

const LogosType* SemaChecker::resolve_type(TinyMapView node) {
    int32_t tc = code_of(node);

    if (tc == la::PTR_TYPE) {
        bool mut = false;
        AnyVal mv = node.get(la::MUTPTR.code);
        if (!mv.is_null() && mv.is_value()) mut = mv.as_value<uint8_t>() != 0;
        auto* inner = node.has_key(la::POINTEE)
                      ? resolve_type(map_of(node.get(la::POINTEE.code)))
                      : error_t();
        return make_ptr(mut, inner);
    }

    if (tc == la::REF_TYPE) {
        auto* inner = node.has_key(la::POINTEE)
                      ? resolve_type(map_of(node.get(la::POINTEE.code)))
                      : error_t();
        std::string lt;
        if (node.has_key(la::LIFETIME))
            lt = std::string(str_of(node.get(la::LIFETIME.code)));
        return make_ref(false, inner, std::move(lt));
    }

    if (tc == la::MUT_REF_TYPE) {
        auto* inner = node.has_key(la::POINTEE)
                      ? resolve_type(map_of(node.get(la::POINTEE.code)))
                      : error_t();
        std::string lt;
        if (node.has_key(la::LIFETIME))
            lt = std::string(str_of(node.get(la::LIFETIME.code)));
        return make_ref(true, inner, std::move(lt));
    }

    if (tc == la::SLICE_TYPE) {
        auto* elem = node.has_key(la::TYPE)
            ? resolve_type(map_of(node.get(la::TYPE.code)))
            : error_t();
        return make_slice_type(elem);
    }

    if (tc == la::TUPLE_TYPE) {
        if (!node.has_key(la::ITEMS))
            return void_t();  // () = unit/void type
        std::vector<const LogosType*> elems;
        auto items = arr_of(node.get(la::ITEMS.code));
        if (items.size() == 0) return void_t();
        for (uint64_t i = 0; i < items.size(); ++i)
            elems.push_back(resolve_type(map_of(items.get(i))));
        return make_tuple_type(std::move(elems));
    }

    if (tc == la::DYN_TYPE) {
        auto tname = std::string(str_of(node.get(la::NAME.code)));
        if (!traits_.count(tname))
            error(std::format("unknown trait '{}' in &dyn type", tname));
        return make_trait_object(tname);
    }

    if (tc == la::TAGGED_TYPE) {
        // &tagged<TS> Trait — thin pointer with tag-based dispatch.
        // struct_name = tag system type name; trait_name = dispatched trait name.
        auto tname = std::string(str_of(node.get(la::NAME.code)));
        if (!traits_.count(tname))
            error(std::format("unknown trait '{}' in &tagged type", tname));
        // Resolve the tag system type (used to check it's a struct/class).
        const LogosType* ts_type = nullptr;
        if (node.has_key(la::TYPE.code))
            ts_type = resolve_type(map_of(node.get(la::TYPE.code)));
        std::string ts_name = ts_type ? ts_type->struct_name : "";
        if (ts_name.empty())
            error("&tagged<TS> Trait: TS must be a concrete struct type");
        LogosType t;
        t.kind       = LogosType::Kind::TaggedPtr;
        t.struct_name = ts_name;   // tag system type name
        t.trait_name  = tname;     // dispatched trait name
        return pool_.alloc(std::move(t));
    }

    if (tc == la::IMPL_TYPE) {
        auto tname = std::string(str_of(node.get(la::NAME.code)));
        LogosType t;
        t.kind = LogosType::Kind::ImplTrait;
        t.struct_name = tname;  // reuse struct_name to store trait name
        return pool_.alloc(std::move(t));
    }

    if (tc == la::CLOSURE_TYPE) {
        LogosType t;
        t.kind = LogosType::Kind::Closure;
        if (node.has_key(la::PARAMS)) {
            auto params_node = map_of(node.get(la::PARAMS.code));
            if (params_node.has_key(la::ITEMS)) {
                auto items = arr_of(params_node.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i)
                    t.closure_params.push_back(resolve_type(map_of(items.get(i))));
            }
        }
        t.closure_ret = node.has_key(la::RET_TYPE)
            ? resolve_type(map_of(node.get(la::RET_TYPE.code)))
            : void_t();
        return pool_.alloc(std::move(t));
    }

    if (tc == la::FN_PTR_TYPE) {
        // fn(T1, T2) -> R — bare function pointer, single ptr in LLVM.
        // Reuse closure_params / closure_ret fields.
        LogosType t;
        t.kind = LogosType::Kind::FnPtr;
        if (node.has_key(la::PARAMS)) {
            auto params_node = map_of(node.get(la::PARAMS.code));
            if (params_node.has_key(la::ITEMS)) {
                auto items = arr_of(params_node.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i)
                    t.closure_params.push_back(resolve_type(map_of(items.get(i))));
            }
        }
        t.closure_ret = node.has_key(la::RET_TYPE)
            ? resolve_type(map_of(node.get(la::RET_TYPE.code)))
            : void_t();
        return pool_.alloc(std::move(t));
    }

    if (tc == la::LIT_INT) {
        auto sv = str_of(node.get(la::VALUE.code));
        LogosType t; t.kind = LogosType::Kind::IntLit;
        t.const_val = parse_int_literal(sv);
        return pool_.alloc(std::move(t));
    }

    if (tc == la::ARR_TYPE) {
        auto* elem = node.has_key(la::TYPE)
                     ? resolve_type(map_of(node.get(la::TYPE.code)))
                     : error_t();
        uint64_t n = 0;
        std::string symbolic;
        if (node.has_key(la::SIZE)) {
            auto av = node.get(la::SIZE.code);
            auto parse_array_size = [](std::string_view sv) -> uint64_t {
                uint64_t r = 0;
                for (char c : sv) {
                    if (c >= '0' && c <= '9') r = r * 10 + (c - '0');
                    else break;
                }
                return r;
            };
            if (av.is_value()) {
                auto sv = str_of(av);
                // If sv starts with a digit, it's a literal size.
                if (!sv.empty() && std::isdigit(sv[0])) {
                    n = parse_array_size(sv);
                } else {
                    // Otherwise, it might be a symbolic constant parameter.
                    symbolic = std::string(sv);
                }
            } else if (av.is_pointer()) {
                // Safety fallback: if it's somehow a string object
                auto sv = str_of(av);
                if (!sv.empty() && std::isdigit(sv[0])) {
                    n = parse_array_size(sv);
                } else {
                    symbolic = std::string(sv);
                }
            }
        }
        return make_array(elem, n, symbolic);
    }

    if (tc == la::ASSOC_TYPE_REF) {
        // base::Item or base::Item<A,B> — associated type reference (plain or GAT)
        auto* base_type = resolve_type(map_of(node.get(la::RECEIVER.code)));
        auto assoc      = std::string(str_of(node.get(la::FIELD.code)));  // "Item"
        // Read GAT type args if present (e.g. T::Item<i32>)
        std::vector<const LogosType*> gat_args;
        if (node.has_key(la::TYPE_PARAMS)) {
            auto tpav = node.get(la::TYPE_PARAMS.code);
            if (!tpav.is_null()) {
                auto tplist = map_of(tpav);
                if (tplist.has_key(la::ITEMS)) {
                    auto items = arr_of(tplist.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < items.size(); ++i) {
                        auto item = map_of(items.get(i));
                        if (code_of(item) == la::LIFETIME_PARAM) continue;
                        gat_args.push_back(resolve_type(item));
                    }
                }
            }
        }
        std::string trait_for_assoc;

        if (base_type->kind == LogosType::Kind::TypeVar) {
            auto& tp_name = base_type->type_var_name;
            if (tp_name == "Self" && !current_trait_name_.empty()) {
                trait_for_assoc = current_trait_name_;
            } else {
                auto bit = current_type_bounds_.find(tp_name);
                if (bit != current_type_bounds_.end()) {
                    // Walk each bound trait AND its supertrait chain to find
                    // the assoc type. A `Container: Datatype` bound pulls in
                    // Datatype's `View` through the supertrait edge.
                    std::vector<std::string> worklist;
                    std::unordered_set<std::string> seen;
                    for (auto& b : bit->second) worklist.push_back(b.trait_name);
                    while (!worklist.empty() && trait_for_assoc.empty()) {
                        std::string tn = std::move(worklist.back());
                        worklist.pop_back();
                        if (!seen.insert(tn).second) continue;
                        auto tit = traits_.find(tn);
                        if (tit == traits_.end()) continue;
                        for (auto& at : tit->second.assoc_types) {
                            if (at.name == assoc) { trait_for_assoc = tn; break; }
                        }
                        if (!trait_for_assoc.empty()) break;
                        for (auto& sup : tit->second.supertraits)
                            worklist.push_back(sup.trait_name);
                    }
                }
            }
        } else if (base_type->kind == LogosType::Kind::AssocType) {
            // T::A::B — search bounds of the associated type itself if we had them,
            // but currently we only store trait_name for the assoc type.
            // We'll search the trait indicated by base_type's own resolution.
            auto tit = traits_.find(base_type->trait_name);
            if (tit != traits_.end()) {
                // This is slightly wrong: T::A might be bound to traits OTHER than the one it's defined in.
                // But our current system doesn't support "type Item: Bound;".
                // So we look in the trait that owns the associated type.
            }
            // Fallback: check all traits implemented by the concrete type if base is already concrete,
            // or just error if we can't find it.
        }

        if (trait_for_assoc.empty()) {
            // Check all traits for ANY type that might have this assoc type (last resort lookup).
            // Try both the full concrete name (e.g. "Box<i32>") and the base struct name ("Box")
            // to handle generic impls like impl<V> Trait for Box<V>.
            std::string cname = type_str(base_type);
            std::string base_name;
            if (base_type->kind == LogosType::Kind::Struct ||
                base_type->kind == LogosType::Kind::Datatype)
                base_name = base_type->struct_name;

            for (auto& [tname, tinfo] : traits_) {
                bool found_impl = impls_.count(tname + "::" + cname) > 0
                               || (!base_name.empty() && impls_.count(tname + "::" + base_name) > 0);
                if (found_impl) {
                    for (auto& at : tinfo.assoc_types) {
                        if (at.name == assoc) { trait_for_assoc = tname; break; }
                    }
                }
                if (!trait_for_assoc.empty()) break;
            }
        }

        if (trait_for_assoc.empty()) {
            error(std::format("no associated type '{}' found for '{}'", assoc, type_str(base_type)));
            return error_t();
        }
        // Bug 5 fix: check GAT arity against the trait's declaration.
        auto tit_gat = traits_.find(trait_for_assoc);
        if (tit_gat != traits_.end()) {
            for (auto& at_def : tit_gat->second.assoc_types) {
                if (at_def.name == assoc) {
                    size_t expected_gat = at_def.type_params.size();
                    if (!at_def.type_params.empty() && gat_args.size() != expected_gat)
                        error(std::format("associated type '{}::{}' expects {} GAT argument(s), got {}",
                                          trait_for_assoc, assoc, expected_gat, gat_args.size()));
                    // Enforce trait bounds on GAT type parameters.
                    if (!at_def.type_params.empty() && gat_args.size() == expected_gat)
                        check_type_bounds(trait_for_assoc + "::" + assoc,
                                          at_def.type_params, gat_args);
                    break;
                }
            }
        }
        LogosType t;
        t.kind            = LogosType::Kind::AssocType;
        t.assoc_base      = base_type;
        t.trait_name      = trait_for_assoc;
        t.assoc_type_name = assoc;
        t.gat_args        = std::move(gat_args);

        auto* result = pool_.alloc(std::move(t));
        // Propagate bounds for T::Item back into the context
        auto tit = traits_.find(trait_for_assoc);
        if (tit != traits_.end()) {
            for (auto& at : tit->second.assoc_types) {
                if (at.name == assoc && !at.bounds.empty()) {
                    current_type_bounds_[type_str(result)] = at.bounds;
                    break;
                }
            }
        }
        return result;
    }

    // <ElemType>[] and <K,V>{} — Hermes typed container type-expressions.
    // Resolved to a special Struct type: struct_name="HermesArr"/"HermesMap",
    // type_args[0] = elem/key type, type_args[1] = val type (map only).
    // The result type of an `as <T>[]` cast is always Hermes (owning zone).
    if (tc == la::HERMES_ARR_TYPE) {
        auto elem_name = str_of(node.get(la::TYPE.code));
        // Resolve element type — must be a known Hermes scalar type name.
        static const std::unordered_map<std::string, const char*> arr_elem_map = {
            {"I8",  "ArrayI8"},  {"U8",  "ArrayU8"},
            {"I16", "ArrayI16"}, {"U16", "ArrayU16"},
            {"U32", "ArrayU32"}, {"I32", "ArrayI32"},
            {"I64", "ArrayI64"}, {"U64", "ArrayU64"},
            {"F32", "ArrayF32"}, {"F64", "ArrayF64"},
        };
        auto it = arr_elem_map.find(std::string(elem_name));
        if (it == arr_elem_map.end()) {
            error(std::format("<{}>[] type: unsupported element type '{}'; "
                              "supported: I8/U8/I16/U16/I32/U32/I64/U64/F32/F64",
                              elem_name, elem_name));
            return error_t();
        }
        // Resolve the underlying logos primitive type for the element.
        const LogosType* elem_t = nullptr;
        if      (elem_name == "I8")  elem_t = prim(LogosType::Kind::I8);
        else if (elem_name == "U8")  elem_t = prim(LogosType::Kind::U8);
        else if (elem_name == "I16") elem_t = prim(LogosType::Kind::I16);
        else if (elem_name == "U16") elem_t = prim(LogosType::Kind::U16);
        else if (elem_name == "U32") elem_t = prim(LogosType::Kind::U32);
        else if (elem_name == "I32") elem_t = prim(LogosType::Kind::I32);
        else if (elem_name == "I64") elem_t = prim(LogosType::Kind::I64);
        else if (elem_name == "U64") elem_t = prim(LogosType::Kind::U64);
        else if (elem_name == "F32") elem_t = prim(LogosType::Kind::F32);
        else if (elem_name == "F64") elem_t = prim(LogosType::Kind::F64);
        else elem_t = error_t();
        // Result type: struct LogosType with special name "HermesArr".
        LogosType t{};
        t.kind = LogosType::Kind::Struct;
        t.struct_name = "HermesArr";
        t.type_args.push_back(elem_t);
        return pool_.alloc(std::move(t));
    }
    if (tc == la::HERMES_MAP_TYPE) {
        auto key_name = str_of(node.get(la::TYPE.code));
        auto val_name = node.has_key(la::RET_TYPE.code)
            ? str_of(node.get(la::RET_TYPE.code)) : std::string_view{"AnyVal"};
        // C6-fix1: removed "Varchar" — it was advertised as supported but key_t
        // resolution only handled "I32", producing silent error_t() for Varchar.
        static const std::unordered_map<std::string, const char*> map_key_map = {
            {"I32", "I32"}, {"U32", "U32"}, {"I64", "I64"}, {"U64", "U64"},
        };
        if (map_key_map.find(std::string(key_name)) == map_key_map.end()) {
            error(std::format("<{},{}>" "{{}} type: unsupported key type '{}'; "
                              "supported: I32/U32/I64/U64", key_name, val_name, key_name));
            return error_t();
        }
        const LogosType* key_t = nullptr;
        if      (key_name == "I32") key_t = prim(LogosType::Kind::I32);
        else if (key_name == "U32") key_t = prim(LogosType::Kind::U32);
        else if (key_name == "I64") key_t = prim(LogosType::Kind::I64);
        else if (key_name == "U64") key_t = prim(LogosType::Kind::U64);
        else key_t = error_t();
        const LogosType* val_t = nullptr;
        if (val_name == "AnyVal") {
            LogosType vt{};
            vt.kind = LogosType::Kind::Struct;
            vt.struct_name = "AnyVal";
            val_t = pool_.alloc(std::move(vt));
        } else {
            // C6-fix2: emit error for unsupported val type (previously silent error_t()).
            error(std::format("<{},{}>" "{{}} type: unsupported val type '{}'; "
                              "supported: AnyVal", key_name, val_name, val_name));
            return error_t();
        }
        LogosType t{};
        t.kind = LogosType::Kind::Struct;
        t.struct_name = "HermesMap";
        t.type_args.push_back(key_t);
        t.type_args.push_back(val_t);
        return pool_.alloc(std::move(t));
    }

    if (tc == la::TYPE_REF) {
        auto name = str_of(node.get(la::NAME.code));
        if (name == "Self") {
            auto tvit = current_type_params_.find("Self");
            if (tvit != current_type_params_.end()) return tvit->second;
        }
        auto* t = lookup_type_by_name(name);
        if (t) return t;
        // Bug 4 fix: give a more informative error when a generic alias is used
        // without its required type arguments.
        auto ait = type_aliases_.find(std::string(name));
        if (ait != type_aliases_.end() &&
            (!ait->second.type_params.empty() || !ait->second.lifetime_params.empty()))
            error(std::format("generic type alias '{}' requires type arguments", name));
        else
            error(std::format("unknown type '{}'", name));
        return error_t();
    }

    if (tc == la::GENERIC_INST) {
        auto name = str_of(node.get(la::NAME.code));

        // Generic type alias: type Foo<T> = Bar<T>;  →  Foo<i32> resolves to Bar<i32>
        {
            auto ait = type_aliases_.find(std::string(name));
            if (ait != type_aliases_.end() &&
                (!ait->second.type_params.empty() || !ait->second.lifetime_params.empty())) {
                // Resolve type and lifetime arguments at the call site.
                std::vector<const LogosType*> args;
                std::vector<std::string> lt_args;
                if (node.has_key(la::ITEMS)) {
                    auto items = arr_of(node.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < items.size(); ++i) {
                        auto item = map_of(items.get(i));
                        if (code_of(item) == la::LIFETIME_PARAM) {
                            lt_args.push_back(std::string(str_of(item.get(la::NAME.code))));
                            continue;
                        }
                        args.push_back(resolve_type(item));
                    }
                }
                size_t expected = ait->second.type_params.size();
                if (args.size() != expected)
                    error(std::format("type alias '{}' expects {} type argument(s), got {}",
                                      name, expected, args.size()));
                size_t lt_expected = ait->second.lifetime_params.size();
                if (lt_args.size() != lt_expected)
                    error(std::format("type alias '{}' expects {} lifetime argument(s), got {}",
                                      name, lt_expected, lt_args.size()));
                SemaSubst s;
                for (size_t i = 0; i < expected && i < args.size(); ++i)
                    s[ait->second.type_params[i].name] = args[i];
                SemaLifetimeSubst ls;
                auto& lparams = ait->second.lifetime_params;
                for (size_t i = 0; i < lparams.size() && i < lt_args.size(); ++i)
                    ls[lparams[i]] = lt_args[i];
                return subst_type_sema(ait->second.type, s, ls);
            }
        }

        // Special case: Box<dyn Trait> = owned trait object (same layout as &dyn Trait)
        if (name == "Box" && node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            if (items.size() == 1) {
                auto* inner = resolve_type(map_of(items.get(0)));
                if (inner && inner->kind == LogosType::Kind::TraitObject)
                    return inner;  // Box<dyn T> ≡ &dyn T in our type system
            }
        }
        bool is_struct = structs_.count(std::string(name)) > 0;
        bool is_dtype  = datatypes_.count(std::string(name)) > 0;
        bool is_enum   = enums_.count(std::string(name)) > 0;
        if (!is_struct && !is_dtype && !is_enum) {
            error(std::format("unknown generic type '{}'", name));
            return error_t();
        }
        // Resolve each type arg (TypeVars in current scope are expanded).
        // Collect LIFETIME_PARAM items ('a) separately — erased at codegen but
        // tracked for borrow checking (struct fields that borrow through a lifetime).
        std::vector<const LogosType*> args;
        std::vector<std::string> lt_args;
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto item = map_of(items.get(i));
                if (code_of(item) == la::LIFETIME_PARAM) {
                    lt_args.push_back(std::string(str_of(item.get(la::NAME.code))));
                    continue;
                }
                args.push_back(resolve_type(item));
            }
        }
        if (is_enum) {
            auto eit = enums_.find(std::string(name));
            if (eit != enums_.end()) check_type_bounds(std::string(name), eit->second.type_params, args);
            LogosType t; t.kind = LogosType::Kind::Enum;
            t.enum_name = std::string(name);
            t.type_args = std::move(args);
            t.lifetime_args = std::move(lt_args);
            return pool_.alloc(std::move(t));
        }
        if (is_dtype) {
            auto dit = datatypes_.find(std::string(name));
            if (dit != datatypes_.end()) check_type_bounds(std::string(name), dit->second.type_params, args);
            LogosType t; t.kind = LogosType::Kind::Datatype;
            t.struct_name = std::string(name);
            t.type_args = std::move(args);
            t.lifetime_args = std::move(lt_args);
            return pool_.alloc(std::move(t));
        }
        auto sit = structs_.find(std::string(name));
        if (sit != structs_.end()) check_type_bounds(std::string(name), sit->second.type_params, args);
        {
            LogosType t; t.kind = LogosType::Kind::Struct;
            t.struct_name = std::string(name);
            t.type_args = std::move(args);
            t.lifetime_args = std::move(lt_args);
            return pool_.alloc(std::move(t));
        }
    }

    error(std::format("unexpected type node code {}", tc));
    return error_t();
}

// ── Lowering helpers ─────────────────────────────────────────────────────────

// Match `concrete` against `pattern`, binding TypeVars.  Minimal mirror of
// mono's match_type — only the cases we need for struct-spec selection.
static bool match_type_sema(const LogosType* concrete, const LogosType* pattern,
                            std::unordered_map<std::string, const LogosType*>& bindings) {
    if (!concrete || !pattern) return false;
    if (pattern->kind == LogosType::Kind::TypeVar) {
        auto it = bindings.find(pattern->type_var_name);
        if (it != bindings.end()) return types_equal(*concrete, *it->second);
        bindings[pattern->type_var_name] = concrete;
        return true;
    }
    if (pattern->kind != concrete->kind) return false;
    switch (pattern->kind) {
    case LogosType::Kind::Ptr:
        return pattern->mut_ptr == concrete->mut_ptr &&
               match_type_sema(concrete->pointee, pattern->pointee, bindings);
    case LogosType::Kind::Ref:
    case LogosType::Kind::MutRef:
        return match_type_sema(concrete->pointee, pattern->pointee, bindings);
    case LogosType::Kind::Array:
        return pattern->arr_size == concrete->arr_size &&
               match_type_sema(concrete->elem, pattern->elem, bindings);
    case LogosType::Kind::Struct:
    case LogosType::Kind::Datatype:
        return pattern->struct_name == concrete->struct_name;
    default:
        return types_equal(*concrete, *pattern);
    }
}

static int specificity_sema(const LogosType* t) {
    if (!t || t->kind == LogosType::Kind::TypeVar) return 0;
    if (t->kind == LogosType::Kind::Ptr)   return 1 + specificity_sema(t->pointee);
    if (t->kind == LogosType::Kind::Array) return 1 + specificity_sema(t->elem);
    return 100;
}

// Find the most specific spec in struct_specs_sema_ whose patterns match
// `type_args` under template `base_name`.  Returns nullptr if none match.
const SemaChecker::SemaStructInfo* SemaChecker::find_best_sema_struct_spec(
        std::string_view base_name,
        const std::vector<const LogosType*>& type_args) {
    const SemaStructInfo* best      = nullptr;
    std::vector<int>      best_vec;
    for (auto& [key, info] : struct_specs_sema_) {
        if (info.base_name != base_name) continue;
        if (info.spec_patterns.size() != type_args.size()) continue;
        std::unordered_map<std::string, const LogosType*> binds;
        bool ok = true;
        std::vector<int> scores(type_args.size());
        for (size_t i = 0; i < type_args.size(); ++i) {
            if (!match_type_sema(type_args[i], info.spec_patterns[i], binds)) { ok = false; break; }
            scores[i] = specificity_sema(info.spec_patterns[i]);
        }
        if (!ok) continue;
        // Lexicographic comparison: prefer higher specificity at earlier positions.
        if (!best || scores > best_vec) { best = &info; best_vec = scores; }
    }
    return best;
}

const LogosType* SemaChecker::field_type_of(std::string_view sname, std::string_view fname) {
    SemaStructInfo* si = nullptr;
    { auto it = structs_.find(std::string(sname)); if (it != structs_.end()) si = &it->second; }
    if (!si) { auto it = datatypes_.find(std::string(sname)); if (it != datatypes_.end()) si = &it->second; }
    if (!si) return nullptr;
    for (auto& f : si->fields) {
        if (f.name == fname) return f.type;
        if (f.is_variadic && fname.starts_with(f.name) && fname.size() > f.name.size() + 1 && fname[f.name.size()] == '_')
            return f.type;
    }
    return nullptr;
}

const LogosType* SemaChecker::field_type_of_for_type(const LogosType* struct_t,
                                             std::string_view fname) {
    if (!struct_t || (struct_t->kind != LogosType::Kind::Struct &&
                      struct_t->kind != LogosType::Kind::Datatype)) return nullptr;
    // Check for a concrete specialization first (including partial specs
    // via pattern matching).
    if (!struct_t->type_args.empty()) {
        if (auto* spec = find_best_sema_struct_spec(struct_t->struct_name, struct_t->type_args)) {
            for (auto& f : spec->fields) {
                if (f.name == fname) return f.type;
                if (f.is_variadic && fname.starts_with(f.name) && fname.size() > f.name.size() + 1 && fname[f.name.size()] == '_')
                    return f.type;
            }
            return nullptr;  // field not in specialization
        }
    }
    auto* raw = field_type_of(struct_t->struct_name, fname);
    if (!raw || struct_t->type_args.empty()) return raw;

    // If it's a variadic expansion (name_N), we need to resolve it against the type arguments.
    if (fname.find('_') != std::string::npos) {
        SemaStructInfo* si2 = nullptr;
        { auto it = structs_.find(struct_t->struct_name); if (it != structs_.end()) si2 = &it->second; }
        if (!si2) { auto it = datatypes_.find(struct_t->struct_name); if (it != datatypes_.end()) si2 = &it->second; }
        if (si2) {
            for (auto& f : si2->fields) {
                if (f.is_variadic && fname.starts_with(f.name) && fname.size() > f.name.size() + 1 && fname[f.name.size()] == '_') {
                    size_t idx = std::stoull(std::string(fname.substr(f.name.size() + 1)));
                    if (f.type && f.type->kind == LogosType::Kind::TypeVar) {
                        for (size_t i = 0, arg_idx = 0; i < si2->type_params.size(); ++i) {
                            if (si2->type_params[i].is_variadic) {
                                if (si2->type_params[i].name == f.type->type_var_name) {
                                    if (arg_idx + idx < struct_t->type_args.size())
                                        return struct_t->type_args[arg_idx + idx];
                                }
                                break;
                            } else {
                                arg_idx++;
                            }
                        }
                    }
                    return raw;
                }
            }
        }
    }

    // Bug 5: look up structs_ OR datatypes_ for the substitution info.
    SemaStructInfo* si2 = nullptr;
    { auto it = structs_.find(struct_t->struct_name); if (it != structs_.end()) si2 = &it->second; }
    if (!si2) { auto it = datatypes_.find(struct_t->struct_name); if (it != datatypes_.end()) si2 = &it->second; }
    if (!si2) return raw;
    SemaSubst subst;
    auto& tps2 = si2->type_params;
    for (size_t i = 0, j = 0; i < tps2.size() && j < struct_t->type_args.size(); ++i) {
        if (tps2[i].is_variadic) break;
        subst[tps2[i].name] = struct_t->type_args[j++];
    }
    // Bug 4: build lifetime substitution so &'z T fields resolve to caller's lifetime.
    SemaLifetimeSubst ls;
    auto& lps = si2->lifetime_params;
    for (size_t i = 0; i < lps.size() && i < struct_t->lifetime_args.size(); ++i)
        ls[lps[i]] = struct_t->lifetime_args[i];
    return subst_type_sema(raw, subst, ls);
}

// ── lower_program and lower_module_items ─────────────────────────────────────

void SemaChecker::lower_program(const std::vector<hermes::Hermes>& asts, lir::LProgram& prog) {
    for (size_t i = 0; i < asts.size(); ++i) {
        holder_ = asts[i].holder();
        file_ = (filenames_ && i < filenames_->size()) ? (*filenames_)[i] : std::string{};
        auto root = asts[i].root_object().as_tiny_map();
        cur_package_ = read_package_name(root);
        lower_module_items(root, prog);
    }
    cur_package_ = {};
}

void SemaChecker::lower_module_items(TinyMapView mod, lir::LProgram& prog) {
    if (!mod.has_key(la::ITEMS)) return;
    auto items = arr_of(mod.get(la::ITEMS.code));

    // Annotations accumulate until the next non-annotation item, then are consumed.
    std::vector<TinyMapView> pending_annots;

    auto apply_annots_to_struct = [&](lir::LStructDef& sd) {
        for (auto& ann : pending_annots) {
            auto aname = std::string(str_of(ann.get(la::NAME.code)));
            if (aname == "type_code" && ann.has_key(la::VALUE)) {
                sd.type_code = read_annotation_u64(ann);
                // Cache with fully-qualified key so type_code_of::<T>() works
                // across packages.  Bare sd.name would collide if two packages
                // define the same struct name with different type_codes.
                auto fqn = cur_package_.empty() ? sd.name
                                                 : cur_package_ + "::" + sd.name;
                explicit_type_codes_[fqn] = sd.type_code;
            }
        }
    };

    auto apply_annots_to_trait = [&](lir::LTraitDef& td) {
        for (auto& ann : pending_annots) {
            auto aname = std::string(str_of(ann.get(la::NAME.code)));
            if (aname == "tag_dispatch" && ann.has_key(la::ARGS)) {
                // ARGS: { ITEMS: [ { NAME: "system_name" } ] }
                auto args_map = map_of(ann.get(la::ARGS.code));
                if (args_map.has_key(la::ITEMS)) {
                    auto arr = arr_of(args_map.get(la::ITEMS.code));
                    if (arr.size() > 0)
                        td.tag_dispatch_system = std::string(str_of(map_of(arr.get(0)).get(la::NAME.code)));
                }
            }
            // #[type_code=N] on a trait makes it a genos: the code identifies
            // the logical datatype family, and each `impl Trait for Eidos`
            // propagates the code to its target struct during lowering.
            else if (aname == "type_code" && ann.has_key(la::VALUE)) {
                td.type_code = read_annotation_u64(ann);
            }
        }
    };

    for (uint64_t i = 0; i < items.size(); ++i) {
        auto item = map_of(items.get(i));
        int32_t c = code_of(item);
        if (c == la::ANNOTATION) {
            pending_annots.push_back(item);
            continue;
        }
        if      (c == la::STRUCT) {
            // Annotations are not applied to structs — only to datatypes.
            if (is_specialization_struct(item))
                prog.struct_specializations.push_back(lower_spec_struct(item));
            else
                prog.structs.push_back(lower_struct_def(item));
        }
        else if (c == la::DATATYPE) {
            if (!item.has_key(la::NAME.code)) {
                // Explicit instantiation declaration: #[type_code=N] datatype SomeType<T>;
                // Has TYPE key, no NAME key, no FIELDS key.
                if (!item.has_key(la::TYPE.code)) {
                    error("explicit instantiation declaration missing type expression");
                } else {
                    auto type_node = map_of(item.get(la::TYPE.code));
                    const LogosType* resolved = resolve_type(type_node);
                    if (resolved && resolved->kind != LogosType::Kind::Error) {
                        lir::LInstAnnotation ia;
                        // Include package prefix for a globally unique canonical name.
                        ia.canonical_name = std::string(cur_package_) + "::" + type_str(resolved);
                        // Mangled name for matching against monomorphized struct defs.
                        if ((resolved->kind == LogosType::Kind::Struct ||
                             resolved->kind == LogosType::Kind::Datatype) &&
                            !resolved->type_args.empty()) {
                            ia.mangled_name = concrete_struct_name(resolved);
                        } else if (resolved->kind == LogosType::Kind::Struct ||
                                   resolved->kind == LogosType::Kind::Datatype) {
                            ia.mangled_name = resolved->struct_name;
                        }
                        for (auto& ann : pending_annots) {
                            auto aname = std::string(str_of(ann.get(la::NAME.code)));
                            if (aname == "type_code" && ann.has_key(la::VALUE)) {
                                ia.type_code = read_annotation_u64(ann);
                            }
                        }
                        // Register into explicit_type_codes_ so sema-time queries
                        // (`type_code_of::<Foo<i32>>()`) resolve to the annotated code.
                        if (ia.type_code != 0)
                            explicit_type_codes_[ia.canonical_name] = ia.type_code;
                        // Store resolved type so mono can demand struct instantiation
                        // even when no Logos code references this type directly.
                        ia.struct_type = resolved;
                        prog.inst_annotations.push_back(std::move(ia));
                    }
                }
            } else if (is_specialization_struct(item)) {
                // Datatype specialization (e.g. `pub eidos Map<Bitmap, V> { ... }`).
                auto sd = lower_spec_struct(item);
                sd.is_datatype = true;
                // Apply #[type_code=N] annotations on full (all-concrete) specs.
                // Without this, `impl Trait for Map<i32, AnyVal>` has no way to
                // find the annotated code during dispatch-entry emission.
                bool all_concrete = !sd.spec_patterns.empty();
                for (auto* p : sd.spec_patterns)
                    if (!p || p->kind == LogosType::Kind::TypeVar) { all_concrete = false; break; }
                if (all_concrete) {
                    for (auto& ann : pending_annots) {
                        auto aname = std::string(str_of(ann.get(la::NAME.code)));
                        if (aname == "type_code" && ann.has_key(la::VALUE)) {
                            sd.type_code = read_annotation_u64(ann);
                            // Register mangled fqn so dispatch-entry lookup
                            // (target = "Map$G2$i32$AnyVal") succeeds.
                            auto* inst_type = make_generic_struct(sd.name, sd.spec_patterns);
                            std::string mangled = concrete_struct_name(inst_type);
                            std::string canon = type_str(inst_type);  // "Name<Args>"
                            auto fqn_mangled = cur_package_.empty()
                                ? mangled : cur_package_ + "::" + mangled;
                            auto fqn_canon = cur_package_.empty()
                                ? canon : cur_package_ + "::" + canon;
                            explicit_type_codes_[fqn_mangled] = sd.type_code;
                            explicit_type_codes_[fqn_canon]   = sd.type_code;
                            // Also register under the template's package (see
                            // matching note in the genos-spec annotation path).
                            std::string tmpl_pkg;
                            auto dit = datatypes_.find(sd.name);
                            if (dit != datatypes_.end()) tmpl_pkg = dit->second.package;
                            else {
                                auto sit = structs_.find(sd.name);
                                if (sit != structs_.end()) tmpl_pkg = sit->second.package;
                            }
                            if (!tmpl_pkg.empty() && tmpl_pkg != cur_package_) {
                                explicit_type_codes_[tmpl_pkg + "::" + mangled] = sd.type_code;
                                explicit_type_codes_[tmpl_pkg + "::" + canon]   = sd.type_code;
                            }
                        }
                    }
                }
                prog.struct_specializations.push_back(std::move(sd));
            } else {
                // Normal datatype definition.
                auto sd = lower_struct_def(item);
                sd.is_datatype   = true;
                // Propagate is_data_plain only for datatypes (not regular structs).
                auto dit = datatypes_.find(sd.name);
                if (dit != datatypes_.end())
                    sd.is_data_plain = dit->second.is_data_plain;
                apply_annots_to_struct(sd);
                // Compute type_hash for concrete (non-generic) datatypes only.
                // Generic templates get their hash at instantiation time in mono_pass.
                if (sd.type_params.empty()) {
                    std::string canon = std::string(cur_package_) + "::" + sd.name;
                    sd.type_hash = type_hash_23(canon);
                    // Auto-assign type_code from hash; ensure it's outside the reserved
                    // inline-AnyVal range 1-127 (those are for zone-stored types >= 128).
                    if (sd.type_code == 0) {
                        uint64_t raw = type_hash_56bit(sd.type_hash);
                        sd.type_code = (raw < 128) ? (raw + 128) : raw;
                    }
                }
                prog.structs.push_back(std::move(sd));
            }
        }
        else if (c == la::ENUM)       prog.enums.push_back(lower_enum_def(item));
        else if (c == la::FN || c == la::EXTERN_FN) {
            if (is_specialization_fn(item))
                prog.specializations.push_back(lower_spec_fn(item));
            else
                prog.functions.push_back(lower_fn(item));
        }
        else if (c == la::CONST_DEF)  prog.consts.push_back(lower_const_def(item));
        else if (c == la::TYPE_ALIAS) prog.type_aliases.push_back(lower_type_alias_def(item));
        else if (c == la::TRAIT_DEF) {
            // Explicit genos specialization decl: `#[type_code=N] pub genos Array<i32>;`.
            // No NAME on the decl (name lives inside TYPE node); type_ref is
            // `Bag<Args>` where Bag is a trait name, not a struct.  Don't call
            // resolve_type — traits aren't types.  Extract trait_name + args
            // manually and canonicalize.
            if (!item.has_key(la::NAME.code) && item.has_key(la::TYPE.code)) {
                auto type_node = map_of(item.get(la::TYPE.code));
                if (code_of(type_node) != la::GENERIC_INST) {
                    error("genos specialization decl must have type arguments");
                } else {
                    auto tname = std::string(str_of(type_node.get(la::NAME.code)));
                    std::string canon = std::string(cur_package_) + "::" + tname + "<";
                    bool arg_ok = true;
                    if (type_node.has_key(la::ITEMS.code)) {
                        auto items2 = arr_of(type_node.get(la::ITEMS.code));
                        for (uint64_t i = 0; i < items2.size(); ++i) {
                            auto* at = resolve_type(map_of(items2.get(i)));
                            if (!at || at->kind == LogosType::Kind::Error) { arg_ok = false; break; }
                            if (i) canon += ", ";
                            canon += type_str(at);
                        }
                    }
                    canon += ">";
                    if (!arg_ok) {
                        error(std::format("genos '{}': cannot resolve type arguments", tname));
                    } else {
                        lir::LInstAnnotation ia;
                        ia.canonical_name = canon;
                        // Also compute the mangled eidos name ("Map$G2$Bitmap$AnyVal")
                        // for an eidos with the same name as the genos — this is the
                        // typical case where `genos Foo<X>` specialization's type_code
                        // should land on the like-named `eidos Foo<X>` struct.
                        auto items2 = arr_of(type_node.get(la::ITEMS.code));
                        std::vector<const LogosType*> resolved_args;
                        for (uint64_t i = 0; i < items2.size(); ++i)
                            resolved_args.push_back(resolve_type(map_of(items2.get(i))));
                        const LogosType* like_eidos = datatypes_.count(tname)
                            ? make_generic_datatype(tname, resolved_args)
                            : (structs_.count(tname)
                                 ? make_generic_struct(tname, resolved_args)
                                 : nullptr);
                        if (like_eidos)
                            ia.mangled_name = concrete_struct_name(like_eidos);
                        for (auto& ann : pending_annots) {
                            auto aname = std::string(str_of(ann.get(la::NAME.code)));
                            if (aname == "type_code" && ann.has_key(la::VALUE)) {
                                ia.type_code = read_annotation_u64(ann);
                            }
                        }
                        if (ia.type_code != 0) {
                            explicit_type_codes_[ia.canonical_name] = ia.type_code;
                            // Register mangled fqn key too so the dispatch-entry
                            // emission (sema_decl.cpp) can find the type_code via
                            // mangled-target lookup.
                            if (!ia.mangled_name.empty())
                                explicit_type_codes_[std::string(cur_package_) + "::" + ia.mangled_name] = ia.type_code;
                            // Also register under the *template's* package, for both
                            // the canonical-name and mangled keys.  Every lookup site
                            // (dispatch-entry emission in sema_decl.cpp, type_code_of
                            // in sema_expr.cpp, etc.) resolves the package via
                            // `datatypes_[base].package` — the template's package.
                            // When a genos specialisation is declared in a *different*
                            // package from its template (e.g. `genos Map<Varchar,
                            // AnyVal>` lives in hermes.objectmap while `datatype
                            // Map<K,V>` lives in hermes.map), lookups keyed by the
                            // template's package would otherwise miss this annotation
                            // and fall back to the auto-hashed type_code, silently
                            // producing a different code at the use site than the
                            // one registered in the dispatch table.  Mirror both keys
                            // under the template's package to keep the two sides in
                            // agreement.
                            std::string tmpl_pkg;
                            auto dit = datatypes_.find(tname);
                            if (dit != datatypes_.end()) tmpl_pkg = dit->second.package;
                            else {
                                auto sit = structs_.find(tname);
                                if (sit != structs_.end()) tmpl_pkg = sit->second.package;
                            }
                            if (!tmpl_pkg.empty() && tmpl_pkg != cur_package_) {
                                // Canonical form: "pkg::Name<Args>".  ia.canonical_name
                                // was built from cur_package_ at the top of this block
                                // (`canon = cur_package_ + "::" + tname + "<…>"`) so we
                                // reconstruct the template-package form by substring.
                                auto colon2 = ia.canonical_name.find("::");
                                if (colon2 != std::string::npos) {
                                    explicit_type_codes_[tmpl_pkg + ia.canonical_name.substr(colon2)] = ia.type_code;
                                }
                                if (!ia.mangled_name.empty())
                                    explicit_type_codes_[tmpl_pkg + "::" + ia.mangled_name] = ia.type_code;
                            }
                        }
                        prog.inst_annotations.push_back(std::move(ia));
                    }
                }
            } else {
                auto td = lower_trait_def(item);
                apply_annots_to_trait(td);
                // Reject #[type_code] on a template genos (type_params present).
                // It would collide at dispatch-table level: every concrete
                // specialization would land in the same tag-system slot.
                if (td.type_code != 0) {
                    auto tit = traits_.find(td.name);
                    if (tit != traits_.end() && !tit->second.type_params.empty())
                        error(std::format("genos '{}': #[type_code] on a template "
                                          "(parametric) genos is forbidden — "
                                          "attach it to a concrete specialization "
                                          "(e.g. `#[type_code=N] genos {}<T>;`)",
                                          td.name, td.name));
                }
                prog.traits.push_back(std::move(td));
            }
        }
        else if (c == la::IMPL_BLOCK) lower_impl_block(item, prog);
        pending_annots.clear();
    }
}

// ── Entry point ───────────────────────────────────────────────────────────────

lir::LProgram sema_lower(const std::vector<logos::hermes::Hermes>& asts,
                          const std::vector<std::string>& filenames) {
    SemaChecker checker;
    return checker.run(asts, filenames);
}

} // namespace logos::compiler
