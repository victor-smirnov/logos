// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Semantic analysis + L-IR lowering.
//
// One pass over the AST: validates the program AND produces a fully-typed
// lir::LProgram.  All LogosType* inside LProgram are owned by
// LProgram::type_pool (moved out of SemaChecker at the end of run()).

#include <logos/compiler/lir.hpp>
#include <logos/compiler/ast.hpp>
#include <logos/hermes/view.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/any_val.hpp>

#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace logos::compiler {

// ── types_equal / type_str ─────────────────────────────────────────────────

bool types_equal(const LogosType& a, const LogosType& b) noexcept {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
    case LogosType::Kind::Ptr:
        return a.mut_ptr == b.mut_ptr &&
               a.pointee && b.pointee &&
               types_equal(*a.pointee, *b.pointee);
    case LogosType::Kind::Array:
        return a.arr_size == b.arr_size &&
               a.elem && b.elem &&
               types_equal(*a.elem, *b.elem);
    case LogosType::Kind::Struct:
        if (a.struct_name != b.struct_name) return false;
        if (a.type_args.size() != b.type_args.size()) return false;
        for (size_t i = 0; i < a.type_args.size(); ++i)
            if (!a.type_args[i] || !b.type_args[i] ||
                !types_equal(*a.type_args[i], *b.type_args[i])) return false;
        return true;
    case LogosType::Kind::Enum:
        return a.enum_name == b.enum_name;
    case LogosType::Kind::TypeVar:
        return a.type_var_name == b.type_var_name;
    default:
        return true;
    }
}

// ── Generic struct name helpers ────────────────────────────────────────────

static std::string mangle_type_for_name(const LogosType* t);

std::string concrete_struct_name(const LogosType* t) {
    if (!t || t->kind != LogosType::Kind::Struct) return {};
    if (t->type_args.empty()) return t->struct_name;
    std::string r = t->struct_name;
    for (auto* a : t->type_args) { r += "__"; r += mangle_type_for_name(a); }
    return r;
}

static std::string mangle_type_for_name(const LogosType* t) {
    if (!t) return "null";
    switch (t->kind) {
    case LogosType::Kind::Ptr:
        return (t->mut_ptr ? "pmut_" : "pcst_") + mangle_type_for_name(t->pointee);
    case LogosType::Kind::Array:
        return "arr" + std::to_string(t->arr_size) + "_" + mangle_type_for_name(t->elem);
    case LogosType::Kind::Struct:
        return concrete_struct_name(t);
    default:
        return type_str(t);  // primitives / TypeVar / Enum already valid identifiers
    }
}

static bool is_integer_kind(LogosType::Kind k) noexcept {
    return k == LogosType::Kind::I32   || k == LogosType::Kind::I64 ||
           k == LogosType::Kind::U8    || k == LogosType::Kind::I8  ||
           k == LogosType::Kind::U32   || k == LogosType::Kind::U64 ||
           k == LogosType::Kind::IntLit || k == LogosType::Kind::Enum;
}

static bool types_compatible(const LogosType* from, const LogosType* to) noexcept {
    if (!from || !to) return false;
    if (types_equal(*from, *to)) return true;
    if (from->kind == LogosType::Kind::IntLit && is_integer_kind(to->kind)) return true;
    if (from->kind == LogosType::Kind::IntLit && to->kind == LogosType::Kind::TypeVar) return true;
    if (from->kind == LogosType::Kind::Enum   && is_integer_kind(to->kind)) return true;
    if (is_integer_kind(from->kind) && to->kind == LogosType::Kind::Enum)   return true;
    if (from->kind == LogosType::Kind::Array &&
        to->kind   == LogosType::Kind::Ptr   &&
        from->elem && to->pointee)
        return types_equal(*from->elem, *to->pointee);
    return false;
}

static const LogosType* unify_int(const LogosType* a, const LogosType* b) noexcept {
    if (a->kind == LogosType::Kind::IntLit) return b;
    return a;
}

std::string type_str(const LogosType* t) {
    if (!t) return "<null>";
    switch (t->kind) {
    case LogosType::Kind::Void:   return "void";
    case LogosType::Kind::I32:    return "i32";
    case LogosType::Kind::I64:    return "i64";
    case LogosType::Kind::F64:    return "f64";
    case LogosType::Kind::Bool:   return "bool";
    case LogosType::Kind::U8:     return "u8";
    case LogosType::Kind::I8:     return "i8";
    case LogosType::Kind::U32:    return "u32";
    case LogosType::Kind::U64:    return "u64";
    case LogosType::Kind::IntLit: return "{integer}";
    case LogosType::Kind::Ptr:
        return std::string(t->mut_ptr ? "*mut " : "*const ") + type_str(t->pointee);
    case LogosType::Kind::Array:
        return std::format("[{}; {}]", type_str(t->elem), t->arr_size);
    case LogosType::Kind::Struct:
        if (t->type_args.empty()) return t->struct_name;
        { std::string r = t->struct_name + "<";
          for (size_t i = 0; i < t->type_args.size(); ++i) {
              if (i) r += ", ";
              r += type_str(t->type_args[i]);
          }
          return r + ">"; }
    case LogosType::Kind::Enum:    return t->enum_name;
    case LogosType::Kind::TypeVar: return std::string(t->type_var_name);
    case LogosType::Kind::Error:   return "<error>";
    }
    return "<unknown>";
}

// ── Implementation ─────────────────────────────────────────────────────────

namespace {

namespace la = logos::compiler::ast;
using hermes::TinyMapView;
using hermes::ArrayView;
using hermes::StringView;
using hermes::AnyVal;
using hermes::MemHolder;

// ── SemaChecker ───────────────────────────────────────────────────────────

class SemaChecker {
public:
    lir::LProgram run(const std::vector<hermes::HermesCtr>& asts,
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
        prog.diags      = std::move(result_);
        prog.type_pool  = std::move(pool_);
        return prog;
    }

private:
    // ── Type pool and primitives ─────────────────────────────────

    TypePool pool_;

    // prims_[int(Kind)] for primitive kinds.  Size 16 (Kind::Error = 15).
    // TypeVar is not a primitive — use make_typevar(name) instead.
    std::array<const LogosType*, 16> prims_{};

    void init_primitives() {
        auto ap = [&](LogosType::Kind k) {
            LogosType t; t.kind = k;
            prims_[int(k)] = pool_.alloc(t);
        };
        ap(LogosType::Kind::Void);
        ap(LogosType::Kind::I32);
        ap(LogosType::Kind::I64);
        ap(LogosType::Kind::F64);
        ap(LogosType::Kind::Bool);
        ap(LogosType::Kind::U8);
        ap(LogosType::Kind::I8);
        ap(LogosType::Kind::U32);
        ap(LogosType::Kind::U64);
        ap(LogosType::Kind::IntLit);
        ap(LogosType::Kind::Error);
    }

    const LogosType* prim(LogosType::Kind k)  { return prims_[int(k)]; }
    const LogosType* void_t()    { return prim(LogosType::Kind::Void); }
    const LogosType* i32_t()     { return prim(LogosType::Kind::I32); }
    const LogosType* bool_t()    { return prim(LogosType::Kind::Bool); }
    const LogosType* u8_t()      { return prim(LogosType::Kind::U8); }
    const LogosType* intlit_t()  { return prim(LogosType::Kind::IntLit); }
    const LogosType* error_t()   { return prim(LogosType::Kind::Error); }

    const LogosType* make_ptr(bool mut, const LogosType* pointee) {
        LogosType t; t.kind = LogosType::Kind::Ptr;
        t.mut_ptr = mut; t.pointee = pointee;
        return pool_.alloc(t);
    }
    const LogosType* make_array(const LogosType* elem, uint64_t n) {
        LogosType t; t.kind = LogosType::Kind::Array;
        t.elem = elem; t.arr_size = n;
        return pool_.alloc(t);
    }
    const LogosType* make_struct_type(std::string_view name) {
        LogosType t; t.kind = LogosType::Kind::Struct; t.struct_name = name;
        return pool_.alloc(std::move(t));
    }
    const LogosType* make_generic_struct(std::string_view name,
                                          std::vector<const LogosType*> args) {
        LogosType t; t.kind = LogosType::Kind::Struct;
        t.struct_name = std::string(name);
        t.type_args   = std::move(args);
        return pool_.alloc(std::move(t));
    }
    const LogosType* make_enum_type(std::string_view name) {
        LogosType t; t.kind = LogosType::Kind::Enum; t.enum_name = name;
        return pool_.alloc(std::move(t));
    }
    const LogosType* make_typevar(std::string_view name) {
        LogosType t; t.kind = LogosType::Kind::TypeVar;
        t.type_var_name = std::string(name);
        return pool_.alloc(t);
    }

    // ── L-IR node factories ──────────────────────────────────────

    template<typename K>
    static lir::LExprPtr make_expr(const LogosType* t, K&& k) {
        auto e = std::make_unique<lir::LExpr>();
        e->type = t;
        e->kind = std::forward<K>(k);
        return e;
    }

    lir::LExprPtr error_expr() {
        return make_expr(error_t(), lir::ELitInt{0});
    }

    template<typename K>
    static lir::LStmt make_stmt(uint32_t line, K&& k) {
        lir::LStmt s; s.line = line;
        s.kind = std::forward<K>(k);
        return s;
    }

    // ── File / line tracking ─────────────────────────────────────

    const std::vector<std::string>* filenames_ = nullptr;
    std::string  file_;
    uint32_t     node_line_ = 0;

    uint32_t get_line(TinyMapView node) noexcept {
        if (node.is_null()) return 0;
        AnyVal av = node.get(la::SRC_LINE.code);
        if (av.is_null() || !av.is_value()) return 0;
        return av.as_value<uint32_t>();
    }

    // ── Hermes helpers ───────────────────────────────────────────

    MemHolder* holder_ = nullptr;

    int32_t code_of(TinyMapView node) noexcept {
        if (node.is_null()) return -1;
        AnyVal av = node.get(la::CODE.code);
        return av.is_null() ? -1 : av.as_value<int32_t>();
    }

    std::string_view str_of(AnyVal av) noexcept {
        if (av.is_null()) return {};
        return StringView(av.to_offset(), holder_).view();
    }

    TinyMapView map_of(AnyVal av) noexcept {
        if (av.is_null()) return TinyMapView{};
        return TinyMapView(av.to_offset(), holder_);
    }

    ArrayView arr_of(AnyVal av) noexcept {
        return ArrayView(av.to_offset(), holder_);
    }

    // ── Diagnostics ──────────────────────────────────────────────

    SemaResult result_;
    std::string ctx_;

    void error(std::string msg) {
        result_.diags.push_back({Diag::Level::Error, ctx_, std::move(msg), file_, node_line_});
    }
    void warn(std::string msg) {
        result_.diags.push_back({Diag::Level::Warning, ctx_, std::move(msg), file_, node_line_});
    }

    // ── Scope management ─────────────────────────────────────────

    struct VarInfo { const LogosType* type; bool is_mut = false; };
    struct Frame   { std::unordered_map<std::string, VarInfo> vars; };
    std::vector<Frame> scope_;

    void push_scope() { scope_.emplace_back(); }
    void pop_scope()  { if (!scope_.empty()) scope_.pop_back(); }

    void define(std::string_view name, const LogosType* t, bool is_mut = false) {
        if (!scope_.empty())
            scope_.back().vars[std::string(name)] = {t, is_mut};
    }

    const LogosType* lookup(std::string_view name) const {
        for (auto it = scope_.rbegin(); it != scope_.rend(); ++it) {
            auto f = it->vars.find(std::string(name));
            if (f != it->vars.end()) return f->second.type;
        }
        auto cit = module_consts_.find(std::string(name));
        if (cit != module_consts_.end()) return cit->second;
        return nullptr;
    }

    bool lookup_is_mut(std::string_view name) const {
        for (auto it = scope_.rbegin(); it != scope_.rend(); ++it) {
            auto f = it->vars.find(std::string(name));
            if (f != it->vars.end()) return f->second.is_mut;
        }
        return false;
    }

    std::string_view struct_name_of(std::string_view var_name) {
        auto* t = lookup(var_name);
        if (!t) return {};
        if (t->kind == LogosType::Kind::Struct) return t->struct_name;
        if (t->kind == LogosType::Kind::Ptr && t->pointee &&
            t->pointee->kind == LogosType::Kind::Struct)
            return t->pointee->struct_name;
        return {};
    }

    std::string_view struct_name_from_type(const LogosType* t) {
        if (!t) return {};
        if (t->kind == LogosType::Kind::Struct) return t->struct_name;
        if (t->kind == LogosType::Kind::Ptr && t->pointee &&
            t->pointee->kind == LogosType::Kind::Struct)
            return t->pointee->struct_name;
        return {};
    }

    // ── Module-level symbol tables ───────────────────────────────

    struct SemaFieldInfo  { std::string_view name; const LogosType* type; };
    struct SemaStructInfo { std::vector<SemaFieldInfo> fields; std::vector<TypeParam> type_params; };
    struct SemaFuncInfo   { std::vector<const LogosType*> param_types; const LogosType* ret_type;
                            std::vector<TypeParam> type_params; };
    struct SemaVariantInfo{ std::string_view name; int32_t value; };
    struct SemaEnumInfo   { std::vector<SemaVariantInfo> variants; };

    // Type params in scope for the function/struct currently being processed.
    // Maps type param name → TypeVar LogosType*.
    std::unordered_map<std::string, const LogosType*> current_type_params_;

    std::unordered_map<std::string, SemaStructInfo>   structs_;
    // concrete_name (e.g. "Pair__i32") → SemaStructInfo for explicit specializations.
    std::unordered_map<std::string, SemaStructInfo>   struct_specs_sema_;
    std::unordered_map<std::string, SemaEnumInfo>     enums_;
    std::unordered_map<std::string, SemaFuncInfo>     funcs_;
    std::unordered_map<std::string, const LogosType*> type_aliases_;
    std::unordered_map<std::string, const LogosType*> module_consts_;

    // ── Type parameter helpers ────────────────────────────────────

    // Read type_param_list from an AST node that may have TYPE_PARAMS.
    std::vector<TypeParam> read_type_params(TinyMapView node) {
        std::vector<TypeParam> result;
        if (!node.has_key(la::TYPE_PARAMS)) return result;
        AnyVal tpav = node.get(la::TYPE_PARAMS.code);
        if (tpav.is_null()) return result;
        // type_param_list => { ITEMS: $... }
        auto tplist = map_of(tpav);
        if (!tplist.has_key(la::ITEMS)) return result;
        auto tpitems = arr_of(tplist.get(la::ITEMS.code));
        for (uint64_t i = 0; i < tpitems.size(); ++i) {
            auto tpnode = map_of(tpitems.get(i));
            if (code_of(tpnode) != la::TYPE_PARAM) continue;
            TypeParam tp;
            tp.name = std::string(str_of(tpnode.get(la::NAME.code)));
            // Optional bounds: ITEMS contains TRAIT_BOUND nodes
            if (tpnode.has_key(la::ITEMS)) {
                auto bounds = arr_of(tpnode.get(la::ITEMS.code));
                for (uint64_t b = 0; b < bounds.size(); ++b) {
                    auto bnode = map_of(bounds.get(b));
                    if (code_of(bnode) == la::TRAIT_BOUND) {
                        TraitBound tb;
                        tb.trait_name = std::string(str_of(bnode.get(la::NAME.code)));
                        tp.bounds.push_back(std::move(tb));
                    }
                }
            }
            result.push_back(std::move(tp));
        }
        return result;
    }

    // Push type params into current_type_params_ (call before resolving fn/struct body).
    void push_type_params(const std::vector<TypeParam>& tps) {
        for (auto& tp : tps)
            current_type_params_[tp.name] = make_typevar(tp.name);
    }
    void pop_type_params(const std::vector<TypeParam>& tps) {
        for (auto& tp : tps)
            current_type_params_.erase(tp.name);
    }

    // ── Sema-side type substitution (TypeVar → concrete) ────────────

    using SemaSubst = std::unordered_map<std::string, const LogosType*>;

    const LogosType* subst_type_sema(const LogosType* t, const SemaSubst& s) {
        if (!t) return t;
        switch (t->kind) {
        case LogosType::Kind::TypeVar: {
            auto it = s.find(t->type_var_name);
            return (it != s.end()) ? it->second : t;
        }
        case LogosType::Kind::Ptr: {
            auto* inner = subst_type_sema(t->pointee, s);
            if (inner == t->pointee) return t;
            return make_ptr(t->mut_ptr, inner);
        }
        case LogosType::Kind::Array: {
            auto* elem = subst_type_sema(t->elem, s);
            if (elem == t->elem) return t;
            return make_array(elem, t->arr_size);
        }
        case LogosType::Kind::Struct: {
            if (t->type_args.empty()) return t;
            std::vector<const LogosType*> new_args;
            bool changed = false;
            for (auto* a : t->type_args) {
                auto* na = subst_type_sema(a, s);
                changed |= (na != a);
                new_args.push_back(na);
            }
            if (!changed) return t;
            return make_generic_struct(t->struct_name, std::move(new_args));
        }
        default: return t;
        }
    }

    // ── Type resolution ──────────────────────────────────────────

    const LogosType* resolve_type(TinyMapView node) {
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

        if (tc == la::ARR_TYPE) {
            auto* elem = node.has_key(la::TYPE)
                         ? resolve_type(map_of(node.get(la::TYPE.code)))
                         : error_t();
            uint64_t n = 0;
            if (node.has_key(la::SIZE)) {
                auto sv = str_of(node.get(la::SIZE.code));
                n = std::strtoull(sv.data(), nullptr, 10);
            }
            return make_array(elem, n);
        }

        if (tc == la::TYPE_REF) {
            auto name = str_of(node.get(la::NAME.code));
            if (name == "i32")  return prim(LogosType::Kind::I32);
            if (name == "i64")  return prim(LogosType::Kind::I64);
            if (name == "f64")  return prim(LogosType::Kind::F64);
            if (name == "bool") return prim(LogosType::Kind::Bool);
            if (name == "u8")   return prim(LogosType::Kind::U8);
            if (name == "i8")   return prim(LogosType::Kind::I8);
            if (name == "u32")  return prim(LogosType::Kind::U32);
            if (name == "u64")  return prim(LogosType::Kind::U64);
            if (name == "void") return prim(LogosType::Kind::Void);
            // Check if it's a type variable in scope
            auto tvit = current_type_params_.find(std::string(name));
            if (tvit != current_type_params_.end()) return tvit->second;
            auto ait = type_aliases_.find(std::string(name));
            if (ait != type_aliases_.end()) return ait->second;
            if (structs_.count(std::string(name))) return make_struct_type(name);
            if (enums_.count(std::string(name)))   return make_enum_type(name);
            error(std::format("unknown type '{}'", name));
            return error_t();
        }

        if (tc == la::GENERIC_INST) {
            auto name = str_of(node.get(la::NAME.code));
            if (!structs_.count(std::string(name))) {
                error(std::format("unknown generic type '{}'", name));
                return error_t();
            }
            // Resolve each type arg (TypeVars in current scope are expanded).
            std::vector<const LogosType*> args;
            if (node.has_key(la::ITEMS)) {
                auto items = arr_of(node.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i)
                    args.push_back(resolve_type(map_of(items.get(i))));
            }
            if (args.empty()) return make_struct_type(name);  // degenerate: no args
            return make_generic_struct(name, std::move(args));
        }

        error(std::format("unexpected type node code {}", tc));
        return error_t();
    }

    // ── Collection phase ─────────────────────────────────────────

    void collect(const std::vector<hermes::HermesCtr>& asts) {
        // First pass: register names (so forward references work).
        for (auto& ast : asts) {
            holder_ = ast.holder();
            auto root = ast.root_object().as_tiny_map();
            if (!root.has_key(la::ITEMS)) continue;
            auto items = arr_of(root.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto item = map_of(items.get(i));
                int32_t ic = code_of(item);
                if (ic == la::STRUCT) {
                    if (is_specialization_struct(item)) continue;  // specs registered later
                    auto sname = std::string(str_of(item.get(la::NAME.code)));
                    if (structs_.count(sname)) error(std::format("duplicate struct '{}'", sname));
                    else structs_[sname] = {};
                } else if (ic == la::ENUM) {
                    auto ename = std::string(str_of(item.get(la::NAME.code)));
                    if (enums_.count(ename)) error(std::format("duplicate enum '{}'", ename));
                    else enums_[ename] = {};
                }
            }
        }
        // Second pass: fill in fields, variants, function signatures.
        for (auto& ast : asts) {
            holder_ = ast.holder();
            auto root = ast.root_object().as_tiny_map();
            collect_module(root);
        }
    }

    void collect_module(TinyMapView mod) {
        if (!mod.has_key(la::ITEMS)) return;
        auto items = arr_of(mod.get(la::ITEMS.code));
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto item = map_of(items.get(i));
            int32_t c = code_of(item);
            if      (c == la::STRUCT) {
                if (is_specialization_struct(item)) collect_struct_spec(item);
                else                                collect_struct(item);
            } else if (c == la::ENUM)                       collect_enum(item);
            else if (c == la::FN || c == la::EXTERN_FN)   collect_fn(item);
            else if (c == la::TYPE_ALIAS)                 collect_type_alias(item);
            else if (c == la::CONST_DEF)                  collect_const(item);
        }
    }

    void collect_enum(TinyMapView node) {
        auto ename = std::string(str_of(node.get(la::NAME.code)));
        ctx_ = std::format("enum {}", ename);
        SemaEnumInfo info;
        int32_t next_val = 0;
        if (node.has_key(la::ITEMS)) {
            auto av = node.get(la::ITEMS.code);
            if (av.is_pointer()) {
                auto list = map_of(av);
                if (list.has_key(la::ITEMS)) {
                    auto variants = arr_of(list.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < variants.size(); ++i) {
                        auto v = map_of(variants.get(i));
                        auto vname = str_of(v.get(la::NAME.code));
                        int32_t vval = next_val;
                        if (v.has_key(la::VALUE)) {
                            auto sv = str_of(v.get(la::VALUE.code));
                            vval = (int32_t)std::strtol(sv.data(), nullptr, 10);
                        }
                        info.variants.push_back({vname, vval});
                        next_val = vval + 1;
                    }
                }
            }
        }
        enums_[ename] = std::move(info);
    }

    void collect_type_alias(TinyMapView node) {
        auto name = std::string(str_of(node.get(la::NAME.code)));
        if (!node.has_key(la::TYPE)) return;
        type_aliases_[name] = resolve_type(map_of(node.get(la::TYPE.code)));
    }

    void collect_const(TinyMapView node) {
        auto name = std::string(str_of(node.get(la::NAME.code)));
        const LogosType* t = nullptr;
        if (node.has_key(la::TYPE)) {
            t = resolve_type(map_of(node.get(la::TYPE.code)));
        } else if (node.has_key(la::VALUE)) {
            // Evaluate type of the value expression lazily.
            // We just store an i32 as placeholder for now.
            t = i32_t();
        }
        if (t) module_consts_[name] = t;
    }

    // Collect a struct specialization into struct_specs_sema_.
    // Only full specializations (all patterns concrete) are registered;
    // partial specs are skipped here and handled by mono.
    void collect_struct_spec(TinyMapView node) {
        auto sname = std::string(str_of(node.get(la::NAME.code)));
        ctx_ = std::format("struct {} (specialization)", sname);

        // Parse spec patterns to determine the concrete name.
        std::vector<const LogosType*> spec_patterns;
        std::vector<TypeParam> pattern_tvars;
        if (node.has_key(la::TYPE_PARAMS)) {
            AnyVal tpav = node.get(la::TYPE_PARAMS.code);
            if (!tpav.is_null()) {
                auto tplist = map_of(tpav);
                if (tplist.has_key(la::ITEMS)) {
                    auto tpitems = arr_of(tplist.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < tpitems.size(); ++i) {
                        auto tpnode = map_of(tpitems.get(i));
                        int32_t tc  = code_of(tpnode);
                        if (tc == la::PTR_TYPE || tc == la::ARR_TYPE) {
                            extract_typevars_from_type_node(tpnode, pattern_tvars);
                            spec_patterns.push_back(resolve_type(tpnode));
                        } else if (tc == la::TYPE_PARAM) {
                            auto name = str_of(tpnode.get(la::NAME.code));
                            auto* known = try_resolve_as_known_type(name);
                            if (known) {
                                spec_patterns.push_back(known);
                            } else {
                                // Partial spec — skip for sema registration.
                                current_type_params_[std::string(name)] = make_typevar(name);
                                pattern_tvars.push_back({std::string(name), {}});
                                spec_patterns.push_back(make_typevar(name));
                            }
                        }
                    }
                }
            }
        }

        // Check if all patterns are concrete (no TypeVar).
        bool all_concrete = true;
        for (auto* p : spec_patterns)
            if (p->kind == LogosType::Kind::TypeVar) { all_concrete = false; break; }

        if (all_concrete) {
            // Compute concrete name (e.g. "Pair__i32") and build SemaStructInfo.
            auto* inst_type = make_generic_struct(sname, spec_patterns);
            std::string concrete = concrete_struct_name(inst_type);

            SemaStructInfo info;
            if (node.has_key(la::FIELDS)) {
                auto fields = arr_of(node.get(la::FIELDS.code));
                for (uint64_t i = 0; i < fields.size(); ++i) {
                    auto fnode = map_of(fields.get(i));
                    auto fname = str_of(fnode.get(la::NAME.code));
                    auto ftype = resolve_type(map_of(fnode.get(la::TYPE.code)));
                    info.fields.push_back({fname, ftype});
                }
            }
            struct_specs_sema_[std::move(concrete)] = std::move(info);
        }

        // Clean up pattern TypeVars.
        for (auto& tp : pattern_tvars)
            current_type_params_.erase(tp.name);
    }

    void collect_struct(TinyMapView node) {
        // Struct specialisations don't go into structs_ — they're lowered directly.
        if (is_specialization_struct(node)) return;
        auto sname = std::string(str_of(node.get(la::NAME.code)));
        ctx_ = std::format("struct {}", sname);
        SemaStructInfo info;
        info.type_params = read_type_params(node);
        push_type_params(info.type_params);
        if (node.has_key(la::FIELDS)) {
            auto fields = arr_of(node.get(la::FIELDS.code));
            for (uint64_t i = 0; i < fields.size(); ++i) {
                auto fnode = map_of(fields.get(i));
                auto fname = str_of(fnode.get(la::NAME.code));
                auto ftype = resolve_type(map_of(fnode.get(la::TYPE.code)));
                info.fields.push_back({fname, ftype});
            }
        }
        pop_type_params(info.type_params);
        structs_[sname] = std::move(info);
        if (node.has_key(la::ITEMS)) {
            auto methods = arr_of(node.get(la::ITEMS.code));
            for (uint64_t m = 0; m < methods.size(); ++m) {
                auto method = map_of(methods.get(m));
                if (code_of(method) == la::FN) collect_fn(method, sname);
            }
        }
    }

    // ── Specialisation helpers ────────────────────────────────────

    // Returns the LogosType* for a name that is a primitive, struct, enum, or alias.
    // Returns nullptr if name is not a known type (i.e. it would be a TypeVar).
    const LogosType* try_resolve_as_known_type(std::string_view name) {
        if (name == "i32")  return prim(LogosType::Kind::I32);
        if (name == "i64")  return prim(LogosType::Kind::I64);
        if (name == "f64")  return prim(LogosType::Kind::F64);
        if (name == "bool") return prim(LogosType::Kind::Bool);
        if (name == "u8")   return prim(LogosType::Kind::U8);
        if (name == "i8")   return prim(LogosType::Kind::I8);
        if (name == "u32")  return prim(LogosType::Kind::U32);
        if (name == "u64")  return prim(LogosType::Kind::U64);
        if (name == "void") return prim(LogosType::Kind::Void);
        auto ait = type_aliases_.find(std::string(name));
        if (ait != type_aliases_.end()) return ait->second;
        if (structs_.count(std::string(name))) return make_struct_type(name);
        if (enums_.count(std::string(name)))   return make_enum_type(name);
        return nullptr;
    }

    bool is_known_type_name(std::string_view name) const {
        static constexpr const char* prims[] = {
            "i32","i64","f64","bool","u8","i8","u32","u64","void",nullptr
        };
        for (int i = 0; prims[i]; ++i) if (prims[i] == name) return true;
        return structs_.count(std::string(name)) ||
               enums_.count(std::string(name))   ||
               type_aliases_.count(std::string(name));
    }

    // Walk a type AST node, registering any unresolved IDENT as a TypeVar in
    // current_type_params_ (used to handle partial-spec patterns like *T or [T;4]).
    void extract_typevars_from_type_node(TinyMapView node,
                                          std::vector<TypeParam>& out_tvars) {
        int32_t tc = code_of(node);
        if (tc == la::PTR_TYPE) {
            if (node.has_key(la::POINTEE))
                extract_typevars_from_type_node(
                    map_of(node.get(la::POINTEE.code)), out_tvars);
        } else if (tc == la::ARR_TYPE) {
            if (node.has_key(la::TYPE))
                extract_typevars_from_type_node(
                    map_of(node.get(la::TYPE.code)), out_tvars);
        } else if (tc == la::TYPE_REF) {
            auto name = str_of(node.get(la::NAME.code));
            if (!is_known_type_name(name) &&
                !current_type_params_.count(std::string(name))) {
                current_type_params_[std::string(name)] = make_typevar(name);
                out_tvars.push_back({std::string(name), {}});
            }
        }
    }

    // Return true when ANY element of the type_param_list is a concrete type
    // or a structured pattern (ptr_type / arr_type), making this a specialisation.
    bool is_specialization_fn(TinyMapView node) {
        if (!node.has_key(la::TYPE_PARAMS)) return false;
        AnyVal tpav = node.get(la::TYPE_PARAMS.code);
        if (tpav.is_null()) return false;
        auto tplist = map_of(tpav);
        if (!tplist.has_key(la::ITEMS)) return false;
        auto items = arr_of(tplist.get(la::ITEMS.code));
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto n = map_of(items.get(i));
            int32_t c = code_of(n);
            if (c == la::PTR_TYPE || c == la::ARR_TYPE)
                return true;  // structured pattern → specialisation
            if (c == la::TYPE_PARAM && !n.has_key(la::ITEMS)) {
                auto name = str_of(n.get(la::NAME.code));
                if (try_resolve_as_known_type(name))
                    return true;  // concrete type name → specialisation
            }
        }
        return false;
    }

    // Same logic as is_specialization_fn but for struct definitions.
    bool is_specialization_struct(TinyMapView node) {
        return is_specialization_fn(node);  // identical check
    }

    // ── lower_spec_struct ─────────────────────────────────────────
    // Lowers a struct specialisation definition.
    // Populates spec_patterns analogously to lower_spec_fn.

    lir::LStructDef lower_spec_struct(TinyMapView node) {
        auto sname = std::string(str_of(node.get(la::NAME.code)));
        ctx_ = std::format("struct {} (specialization)", sname);

        lir::LStructDef sd;
        sd.name = sname;
        sd.is_specialization = true;

        // Parse spec type-param list: populate spec_patterns and TypeVar scope.
        std::vector<TypeParam> pattern_tvars;
        if (node.has_key(la::TYPE_PARAMS)) {
            AnyVal tpav = node.get(la::TYPE_PARAMS.code);
            if (!tpav.is_null()) {
                auto tplist = map_of(tpav);
                if (tplist.has_key(la::ITEMS)) {
                    auto tpitems = arr_of(tplist.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < tpitems.size(); ++i) {
                        auto tpnode = map_of(tpitems.get(i));
                        int32_t tc  = code_of(tpnode);
                        if (tc == la::PTR_TYPE || tc == la::ARR_TYPE) {
                            extract_typevars_from_type_node(tpnode, pattern_tvars);
                            sd.spec_patterns.push_back(resolve_type(tpnode));
                        } else if (tc == la::TYPE_PARAM) {
                            auto name = str_of(tpnode.get(la::NAME.code));
                            if (tpnode.has_key(la::ITEMS)) {
                                // TypeVar with bounds — stays TypeVar in pattern.
                                current_type_params_[std::string(name)] = make_typevar(name);
                                TypeParam tp; tp.name = std::string(name);
                                pattern_tvars.push_back(std::move(tp));
                                sd.spec_patterns.push_back(make_typevar(name));
                            } else {
                                auto* known = try_resolve_as_known_type(name);
                                if (known) {
                                    sd.spec_patterns.push_back(known);
                                } else {
                                    current_type_params_[std::string(name)] = make_typevar(name);
                                    pattern_tvars.push_back({std::string(name), {}});
                                    sd.spec_patterns.push_back(make_typevar(name));
                                }
                            }
                        }
                    }
                }
            }
        }

        // Lower fields (TypeVars from patterns now in scope).
        if (node.has_key(la::FIELDS)) {
            auto fields = arr_of(node.get(la::FIELDS.code));
            for (uint64_t i = 0; i < fields.size(); ++i) {
                auto fnode = map_of(fields.get(i));
                auto fname = str_of(fnode.get(la::NAME.code));
                auto ftype = resolve_type(map_of(fnode.get(la::TYPE.code)));
                sd.fields.push_back({std::string(fname), ftype});
            }
        }

        // Lower methods.
        if (node.has_key(la::ITEMS)) {
            auto methods = arr_of(node.get(la::ITEMS.code));
            for (uint64_t m = 0; m < methods.size(); ++m) {
                auto method = map_of(methods.get(m));
                if (code_of(method) == la::FN)
                    sd.methods.push_back(lower_fn(method, sname));
            }
        }

        // Clean up pattern TypeVars.
        for (auto& tp : pattern_tvars)
            current_type_params_.erase(tp.name);

        return sd;
    }

    // ── lower_spec_fn ─────────────────────────────────────────────
    // Like lower_fn but for specialisation definitions.
    // Populates spec_patterns and routes the result to prog.specialisations.

    lir::LFunction lower_spec_fn(TinyMapView node) {
        auto raw_name = str_of(node.get(la::NAME.code));
        ctx_ = std::format("fn {} (specialization)", raw_name);
        node_line_ = get_line(node);

        lir::LFunction fn;
        fn.name = std::string(raw_name);
        fn.is_specialization = true;

        // Parse spec type-param list: populate fn.spec_patterns and scope TypeVars.
        std::vector<TypeParam> pattern_tvars;
        if (node.has_key(la::TYPE_PARAMS)) {
            AnyVal tpav = node.get(la::TYPE_PARAMS.code);
            if (!tpav.is_null()) {
                auto tplist = map_of(tpav);
                if (tplist.has_key(la::ITEMS)) {
                    auto tpitems = arr_of(tplist.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < tpitems.size(); ++i) {
                        auto tpnode = map_of(tpitems.get(i));
                        int32_t tc  = code_of(tpnode);

                        if (tc == la::PTR_TYPE || tc == la::ARR_TYPE) {
                            // Structured pattern: extract TypeVars then resolve.
                            extract_typevars_from_type_node(tpnode, pattern_tvars);
                            fn.spec_patterns.push_back(resolve_type(tpnode));

                        } else if (tc == la::TYPE_PARAM) {
                            auto name = str_of(tpnode.get(la::NAME.code));
                            if (tpnode.has_key(la::ITEMS)) {
                                // TypeVar with bounds — still a TypeVar in patterns.
                                current_type_params_[std::string(name)] =
                                    make_typevar(name);
                                TypeParam tp; tp.name = std::string(name);
                                // Read bounds for completeness.
                                auto bounds = arr_of(tpnode.get(la::ITEMS.code));
                                for (uint64_t b = 0; b < bounds.size(); ++b) {
                                    auto bn = map_of(bounds.get(b));
                                    if (code_of(bn) == la::TRAIT_BOUND)
                                        tp.bounds.push_back(
                                            {std::string(str_of(bn.get(la::NAME.code)))});
                                }
                                pattern_tvars.push_back(std::move(tp));
                                fn.spec_patterns.push_back(make_typevar(name));
                            } else {
                                // Plain IDENT: known type → concrete; else → TypeVar.
                                auto* known = try_resolve_as_known_type(name);
                                if (known) {
                                    fn.spec_patterns.push_back(known);
                                } else {
                                    current_type_params_[std::string(name)] =
                                        make_typevar(name);
                                    pattern_tvars.push_back({std::string(name), {}});
                                    fn.spec_patterns.push_back(make_typevar(name));
                                }
                            }
                        }
                    }
                }
            }
        }

        // Resolve params and return type (TypeVars from patterns are now in scope).
        fn.ret_type = node.has_key(la::RET_TYPE)
            ? resolve_type(map_of(node.get(la::RET_TYPE.code)))
            : void_t();
        ret_type_ = fn.ret_type;

        scope_.clear();
        push_scope();

        if (node.has_key(la::PARAMS)) {
            auto params_av = node.get(la::PARAMS.code);
            if (params_av.is_pointer()) {
                auto params_node = map_of(params_av);
                if (params_node.has_key(la::ITEMS)) {
                    auto arr = arr_of(params_node.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < arr.size(); ++i) {
                        auto p = map_of(arr.get(i));
                        if (code_of(p) != la::PARAM) continue;
                        auto pname = str_of(p.get(la::NAME.code));
                        auto ptype = resolve_type(map_of(p.get(la::TYPE.code)));
                        define(pname, ptype);
                        fn.params.push_back({std::string(pname), ptype});
                    }
                }
            }
        }

        if (!fn.is_extern && node.has_key(la::BODY)) {
            auto body_node = map_of(node.get(la::BODY.code));
            fn.body = lower_block(body_node);
            if (fn.ret_type && fn.ret_type->kind != LogosType::Kind::Void &&
                fn.ret_type->kind != LogosType::Kind::Error &&
                !block_always_returns(body_node)) {
                error("not all paths return a value");
            }
        }

        pop_scope();

        // Remove pattern TypeVars from scope.
        for (auto& tp : pattern_tvars)
            current_type_params_.erase(tp.name);

        return fn;
    }

    void collect_fn(TinyMapView node, std::string_view struct_ctx = {}) {
        auto raw_name = str_of(node.get(la::NAME.code));
        std::string mangled = struct_ctx.empty()
            ? std::string(raw_name)
            : std::string(struct_ctx) + "__" + std::string(raw_name);
        ctx_ = std::format("fn {}", mangled);

        // Specialisations are validated and lowered inline by lower_spec_fn;
        // skip collection-phase registration entirely.
        if (is_specialization_fn(node)) return;

        if (funcs_.count(mangled)) {
            error(std::format("duplicate function '{}'", mangled));
            return;
        }
        SemaFuncInfo info;
        info.type_params = read_type_params(node);
        push_type_params(info.type_params);
        if (node.has_key(la::PARAMS)) {
            auto params_av = node.get(la::PARAMS.code);
            if (params_av.is_pointer()) {
                auto params_node = map_of(params_av);
                if (params_node.has_key(la::ITEMS)) {
                    auto arr = arr_of(params_node.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < arr.size(); ++i) {
                        auto p = map_of(arr.get(i));
                        if (code_of(p) != la::PARAM) continue;
                        info.param_types.push_back(resolve_type(map_of(p.get(la::TYPE.code))));
                    }
                }
            }
        }
        info.ret_type = node.has_key(la::RET_TYPE)
            ? resolve_type(map_of(node.get(la::RET_TYPE.code)))
            : void_t();
        pop_type_params(info.type_params);
        funcs_[mangled] = std::move(info);
    }

    // ── Loop depth / return type ─────────────────────────────────

    int loop_depth_ = 0;
    const LogosType* ret_type_ = nullptr;

    // ── Return reachability (on AST nodes) ───────────────────────

    bool stmt_always_returns(TinyMapView stmt) {
        int32_t c = code_of(stmt);
        if (c == la::RETURN) return true;
        if (c == la::LOOP) {
            return stmt.has_key(la::BODY) &&
                   block_always_returns(map_of(stmt.get(la::BODY.code)));
        }
        if (c == la::IF) {
            if (!stmt.has_key(la::ELSE)) return false;
            bool then_ret = stmt.has_key(la::THEN) &&
                            block_always_returns(map_of(stmt.get(la::THEN.code)));
            auto else_node = map_of(stmt.get(la::ELSE.code));
            bool else_ret  = (code_of(else_node) == la::BLOCK)
                             ? block_always_returns(else_node)
                             : stmt_always_returns(else_node);
            return then_ret && else_ret;
        }
        if (c == la::MATCH) {
            if (!stmt.has_key(la::ITEMS)) return false;
            auto arms = arr_of(stmt.get(la::ITEMS.code));
            bool has_wild = false, all_ret = true;
            for (uint64_t i = 0; i < arms.size(); ++i) {
                auto arm = map_of(arms.get(i));
                if (code_of(arm) != la::MATCH_ARM) continue;
                if (arm.has_key(la::LHS)) {
                    auto pat = map_of(arm.get(la::LHS.code));
                    if (code_of(pat) == la::PAT_WILD) has_wild = true;
                }
                if (arm.has_key(la::BODY)) {
                    auto body = map_of(arm.get(la::BODY.code));
                    bool arm_ret = (code_of(body) == la::BLOCK)
                                   ? block_always_returns(body)
                                   : stmt_always_returns(body);
                    if (!arm_ret) all_ret = false;
                } else { all_ret = false; }
            }
            return has_wild && all_ret;
        }
        return false;
    }

    bool block_always_returns(TinyMapView block) {
        if (!block.has_key(la::ITEMS)) return false;
        auto stmts = arr_of(block.get(la::ITEMS.code));
        for (uint64_t i = 0; i < stmts.size(); ++i) {
            auto s = map_of(stmts.get(i));
            if (!s.is_null() && stmt_always_returns(s)) return true;
        }
        return false;
    }

    // ── Lowering helpers ─────────────────────────────────────────

    static bool is_numeric(const LogosType* t) noexcept {
        if (!t) return false;
        return t->kind == LogosType::Kind::F64 ||
               t->kind == LogosType::Kind::TypeVar ||
               is_integer_kind(t->kind);
    }
    static bool is_integer(const LogosType* t) noexcept {
        return t && is_integer_kind(t->kind);
    }

    // Raw field type from the template (may contain TypeVars).
    const LogosType* field_type_of(std::string_view sname, std::string_view fname) {
        auto sit = structs_.find(std::string(sname));
        if (sit == structs_.end()) return nullptr;
        for (auto& f : sit->second.fields)
            if (f.name == fname) return f.type;
        return nullptr;
    }

    // Field type with TypeVars substituted for a (possibly generic) struct instance.
    const LogosType* field_type_of_for_type(const LogosType* struct_t,
                                             std::string_view fname) {
        if (!struct_t || struct_t->kind != LogosType::Kind::Struct) return nullptr;
        // Check for a concrete specialization first.
        if (!struct_t->type_args.empty()) {
            std::string concrete = concrete_struct_name(struct_t);
            auto spec_it = struct_specs_sema_.find(concrete);
            if (spec_it != struct_specs_sema_.end()) {
                for (auto& f : spec_it->second.fields)
                    if (f.name == fname) return f.type;
                return nullptr;  // field not in specialization
            }
        }
        auto* raw = field_type_of(struct_t->struct_name, fname);
        if (!raw || struct_t->type_args.empty()) return raw;
        // Build substitution from template type_params → concrete type_args.
        auto sit = structs_.find(struct_t->struct_name);
        if (sit == structs_.end()) return raw;
        SemaSubst subst;
        auto& tps = sit->second.type_params;
        for (size_t i = 0; i < tps.size() && i < struct_t->type_args.size(); ++i)
            subst[tps[i].name] = struct_t->type_args[i];
        return subst_type_sema(raw, subst);
    }

    // ── lower_expr ───────────────────────────────────────────────

    lir::LExprPtr lower_expr(TinyMapView expr) {
        if (expr.is_null()) return error_expr();
        node_line_ = get_line(expr);
        int32_t c = code_of(expr);

        switch (c) {

        case la::LIT_INT: {
            auto sv = str_of(expr.get(la::VALUE.code));
            int64_t v = std::strtoll(sv.data(), nullptr, 10);
            return make_expr(intlit_t(), lir::ELitInt{v});
        }
        case la::LIT_BOOL: {
            AnyVal av = expr.get(la::VALUE.code);
            bool v = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
            return make_expr(bool_t(), lir::ELitBool{v});
        }
        case la::LIT_STR: {
            auto sv = str_of(expr.get(la::VALUE.code));
            return make_expr(make_ptr(false, u8_t()), lir::ELitStr{std::string(sv)});
        }

        case la::VAR_REF: {
            auto name = str_of(expr.get(la::NAME.code));
            auto* t = lookup(name);
            if (!t) {
                error(std::format("undefined variable '{}'", name));
                return error_expr();
            }
            return make_expr(t, lir::EVarRef{std::string(name)});
        }

        case la::PAREN_EXPR:
            if (expr.has_key(la::VALUE))
                return lower_expr(map_of(expr.get(la::VALUE.code)));
            return error_expr();

        case la::CAST: {
            lir::LExprPtr inner = expr.has_key(la::VALUE)
                ? lower_expr(map_of(expr.get(la::VALUE.code)))
                : error_expr();
            const LogosType* target = expr.has_key(la::TYPE)
                ? resolve_type(map_of(expr.get(la::TYPE.code)))
                : error_t();
            return make_expr(target, lir::ECast{std::move(inner)});
        }

        case la::BINOP:       return lower_binop(expr);
        case la::UNARY:       return lower_unary(expr);
        case la::DEREF:       return lower_deref(expr);
        case la::CALL:         return lower_call(expr);
        case la::GENERIC_CALL: return lower_generic_call(expr);
        case la::METHOD_CALL:  return lower_method_call(expr);
        case la::FIELD_READ:  return lower_field_read(expr);
        case la::STRUCT_LIT:  return lower_struct_lit(expr);
        case la::INDEX_READ:  return lower_index_read(expr);
        case la::ARR_LIT:     return lower_arr_lit(expr);
        case la::ENUM_LIT:    return lower_enum_lit(expr);

        default:
            return error_expr();
        }
    }

    lir::LExprPtr lower_binop(TinyMapView node) {
        auto op  = str_of(node.get(la::OP.code));
        auto lhs = lower_expr(map_of(node.get(la::LHS.code)));
        auto rhs = lower_expr(map_of(node.get(la::RHS.code)));
        auto* lt = lhs->type;
        auto* rt = rhs->type;

        const LogosType* result_type = error_t();

        if (lt->kind == LogosType::Kind::Error || rt->kind == LogosType::Kind::Error) {
            result_type = error_t();
        } else if (op == "&&" || op == "||") {
            if (lt->kind != LogosType::Kind::Bool)
                error(std::format("operator '{}': left must be bool, got {}", op, type_str(lt)));
            if (rt->kind != LogosType::Kind::Bool)
                error(std::format("operator '{}': right must be bool, got {}", op, type_str(rt)));
            result_type = bool_t();
        } else if (op == "==" || op == "!=" ||
                   op == "<"  || op == "<=" || op == ">" || op == ">=") {
            bool ok = types_compatible(lt, rt) || types_compatible(rt, lt);
            if (!ok)
                error(std::format("operator '{}': type mismatch ({} vs {})",
                      op, type_str(lt), type_str(rt)));
            result_type = bool_t();
        } else if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%") {
            if (!is_numeric(lt))
                error(std::format("operator '{}': left must be numeric, got {}", op, type_str(lt)));
            if (!is_numeric(rt))
                error(std::format("operator '{}': right must be numeric, got {}", op, type_str(rt)));
            bool both_int = is_integer_kind(lt->kind) && is_integer_kind(rt->kind);
            if (!both_int) {
                bool compat = types_compatible(lt, rt) || types_compatible(rt, lt);
                if (is_numeric(lt) && is_numeric(rt) && !compat)
                    error(std::format("operator '{}': type mismatch ({} vs {})",
                          op, type_str(lt), type_str(rt)));
                // If one side is TypeVar and the other is IntLit, result is the TypeVar
                if (lt->kind == LogosType::Kind::TypeVar) result_type = lt;
                else if (rt->kind == LogosType::Kind::TypeVar) result_type = rt;
                else result_type = lt;
            } else {
                if (!types_compatible(lt, rt) && !types_compatible(rt, lt))
                    error(std::format("operator '{}': type mismatch ({} vs {})",
                          op, type_str(lt), type_str(rt)));
                result_type = unify_int(lt, rt);
            }
        } else {
            error(std::format("unknown binary operator '{}'", op));
        }

        return make_expr(result_type, lir::EBinOp{std::string(op), std::move(lhs), std::move(rhs)});
    }

    lir::LExprPtr lower_unary(TinyMapView node) {
        auto op  = str_of(node.get(la::OP.code));

        // & — address-of: return alloca pointer without evaluating operand
        if (op == "&") {
            auto child = map_of(node.get(la::VALUE.code));
            if (code_of(child) != la::VAR_REF) {
                error("'&' operand must be a variable");
                return error_expr();
            }
            auto var_name = str_of(child.get(la::NAME.code));
            auto* vt = lookup(var_name);
            if (!vt) {
                error(std::format("'&': undefined variable '{}'", var_name));
                return error_expr();
            }
            return make_expr(make_ptr(false, vt), lir::EAddrOf{std::string(var_name)});
        }

        auto operand = lower_expr(map_of(node.get(la::VALUE.code)));
        auto* vt = operand->type;
        if (vt->kind == LogosType::Kind::Error)
            return make_expr(error_t(), lir::EUnary{std::string(op), std::move(operand)});

        const LogosType* result_type = error_t();
        if (op == "-") {
            if (!is_numeric(vt))
                error(std::format("unary '-': operand must be numeric, got {}", type_str(vt)));
            result_type = vt;
        } else if (op == "!") {
            if (vt->kind != LogosType::Kind::Bool)
                error(std::format("unary '!': operand must be bool, got {}", type_str(vt)));
            result_type = bool_t();
        } else {
            error(std::format("unknown unary operator '{}'", op));
        }

        return make_expr(result_type, lir::EUnary{std::string(op), std::move(operand)});
    }

    lir::LExprPtr lower_deref(TinyMapView node) {
        auto operand = lower_expr(map_of(node.get(la::VALUE.code)));
        auto* vt = operand->type;
        if (vt->kind == LogosType::Kind::Error)
            return make_expr(error_t(), lir::EDeref{std::move(operand)});
        if (vt->kind != LogosType::Kind::Ptr) {
            error(std::format("dereference of non-pointer type {}", type_str(vt)));
            return make_expr(error_t(), lir::EDeref{std::move(operand)});
        }
        auto* res = vt->pointee ? vt->pointee : error_t();
        return make_expr(res, lir::EDeref{std::move(operand)});
    }

    lir::LExprPtr lower_call(TinyMapView node) {
        auto callee = str_of(node.get(la::CALLEE.code));
        auto fit = funcs_.find(std::string(callee));

        std::vector<lir::LExprPtr> arg_exprs;
        if (node.has_key(la::ARGS)) {
            auto args = arr_of(node.get(la::ARGS.code));
            for (uint64_t i = 0; i < args.size(); ++i)
                arg_exprs.push_back(lower_expr(map_of(args.get(i))));
        }

        if (fit == funcs_.end()) {
            error(std::format("call to undefined function '{}'", callee));
            return make_expr(error_t(), lir::ECall{std::string(callee), {}, std::move(arg_exprs)});
        }

        auto& fi = fit->second;
        uint64_t n_args = arg_exprs.size();

        if (n_args != fi.param_types.size()) {
            error(std::format("call to '{}': expected {} args, got {}",
                  callee, fi.param_types.size(), n_args));
        } else {
            for (uint64_t i = 0; i < n_args; ++i) {
                auto* at = arg_exprs[i]->type;
                auto* pt = fi.param_types[i];
                if (at->kind != LogosType::Kind::Error &&
                    pt->kind != LogosType::Kind::Error &&
                    !types_compatible(at, pt))
                    error(std::format("call to '{}' arg {}: expected {}, got {}",
                          callee, i + 1, type_str(pt), type_str(at)));
            }
        }

        return make_expr(fi.ret_type, lir::ECall{std::string(callee), {}, std::move(arg_exprs)});
    }

    // ── lower_generic_call: foo::<T1, T2>(args) ──────────────────

    lir::LExprPtr lower_generic_call(TinyMapView node) {
        auto callee = str_of(node.get(la::CALLEE.code));
        auto fit = funcs_.find(std::string(callee));

        // Resolve type arguments from TYPE_PARAMS (type_arg_list with ITEMS)
        std::vector<const LogosType*> type_args;
        if (node.has_key(la::TYPE_PARAMS)) {
            AnyVal tpav = node.get(la::TYPE_PARAMS.code);
            if (!tpav.is_null()) {
                auto tplist = map_of(tpav);
                if (tplist.has_key(la::ITEMS)) {
                    auto items = arr_of(tplist.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < items.size(); ++i)
                        type_args.push_back(resolve_type(map_of(items.get(i))));
                }
            }
        }

        // Resolve value arguments from ARGS (call_arg_list? with ITEMS, or null)
        std::vector<lir::LExprPtr> arg_exprs;
        if (node.has_key(la::ARGS)) {
            AnyVal args_av = node.get(la::ARGS.code);
            if (!args_av.is_null()) {
                auto args_list = map_of(args_av);
                if (args_list.has_key(la::ITEMS)) {
                    auto items = arr_of(args_list.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < items.size(); ++i)
                        arg_exprs.push_back(lower_expr(map_of(items.get(i))));
                }
            }
        }

        if (fit == funcs_.end()) {
            error(std::format("call to undefined function '{}'", callee));
            return make_expr(error_t(), lir::ECall{std::string(callee), std::move(type_args), std::move(arg_exprs)});
        }

        auto& fi = fit->second;

        // Validate type argument count against type parameters
        if (!fi.type_params.empty() && type_args.size() != fi.type_params.size()) {
            error(std::format("call to '{}': expected {} type arg(s), got {}",
                  callee, fi.type_params.size(), type_args.size()));
        }

        // Build substitution map: type param name → concrete type arg
        std::unordered_map<std::string, const LogosType*> subst;
        size_t n_subst = std::min(fi.type_params.size(), type_args.size());
        for (size_t i = 0; i < n_subst; ++i)
            subst[fi.type_params[i].name] = type_args[i];

        // Determine concrete return type by substituting TypeVars (recursive)
        const LogosType* ret = subst_type_sema(fi.ret_type, subst);

        // Validate value argument count
        uint64_t n_args = arg_exprs.size();
        if (n_args != fi.param_types.size()) {
            error(std::format("call to '{}': expected {} args, got {}",
                  callee, fi.param_types.size(), n_args));
        } else {
            for (uint64_t i = 0; i < n_args; ++i) {
                auto* at = arg_exprs[i]->type;
                auto* pt = subst_type_sema(fi.param_types[i], subst);
                if (at->kind != LogosType::Kind::Error &&
                    pt->kind != LogosType::Kind::Error &&
                    pt->kind != LogosType::Kind::TypeVar &&
                    !types_compatible(at, pt))
                    error(std::format("call to '{}' arg {}: expected {}, got {}",
                          callee, i + 1, type_str(pt), type_str(at)));
            }
        }

        return make_expr(ret, lir::ECall{std::string(callee), std::move(type_args), std::move(arg_exprs)});
    }

    lir::LExprPtr lower_method_call(TinyMapView node) {
        auto method_name = str_of(node.get(la::NAME.code));
        auto recv = lower_expr(map_of(node.get(la::RECEIVER.code)));
        auto sname = struct_name_from_type(recv->type);

        std::vector<lir::LExprPtr> arg_exprs;
        if (node.has_key(la::ARGS)) {
            auto args = arr_of(node.get(la::ARGS.code));
            for (uint64_t i = 0; i < args.size(); ++i)
                arg_exprs.push_back(lower_expr(map_of(args.get(i))));
        }

        if (sname.empty()) {
            error(std::format("method call: receiver is not a struct (got {})",
                  type_str(recv->type)));
            return make_expr(error_t(),
                lir::EMethodCall{std::move(recv), std::string(method_name), std::move(arg_exprs)});
        }

        auto mangled = std::string(sname) + "__" + std::string(method_name);
        auto fit = funcs_.find(mangled);
        if (fit == funcs_.end()) {
            error(std::format("method call: '{}' has no method '{}'", sname, method_name));
            return make_expr(error_t(),
                lir::EMethodCall{std::move(recv), std::string(method_name), std::move(arg_exprs)});
        }

        auto& fi = fit->second;
        uint64_t explicit_args = arg_exprs.size();
        size_t expected_explicit = fi.param_types.size() > 0 ? fi.param_types.size() - 1 : 0;
        if (explicit_args != expected_explicit)
            error(std::format("method call '{}': expected {} args, got {}",
                  mangled, expected_explicit, explicit_args));
        else {
            for (uint64_t i = 0; i < explicit_args; ++i) {
                auto* at = arg_exprs[i]->type;
                size_t pi = i + 1;
                if (pi < fi.param_types.size()) {
                    auto* pt = fi.param_types[pi];
                    if (at->kind != LogosType::Kind::Error && pt->kind != LogosType::Kind::Error &&
                        !types_compatible(at, pt))
                        error(std::format("method '{}' arg {}: expected {}, got {}",
                              mangled, i + 1, type_str(pt), type_str(at)));
                }
            }
        }

        return make_expr(fi.ret_type,
            lir::EMethodCall{std::move(recv), std::string(method_name), std::move(arg_exprs)});
    }

    lir::LExprPtr lower_field_read(TinyMapView node) {
        auto field_name = str_of(node.get(la::FIELD.code));
        auto recv = lower_expr(map_of(node.get(la::RECEIVER.code)));
        auto sname = struct_name_from_type(recv->type);
        if (sname.empty()) {
            error(std::format("field read: receiver is not a struct (got {})",
                  type_str(recv->type)));
            return make_expr(error_t(), lir::EFieldRead{std::move(recv), std::string(field_name)});
        }
        // Resolve the actual struct type (receiver may be a pointer to a struct).
        const LogosType* recv_struct_t = recv->type;
        if (recv_struct_t->kind == LogosType::Kind::Ptr)
            recv_struct_t = recv_struct_t->pointee;
        auto* ft = field_type_of_for_type(recv_struct_t, field_name);
        if (!ft) {
            error(std::format("field read: struct '{}' has no field '{}'", sname, field_name));
            return make_expr(error_t(), lir::EFieldRead{std::move(recv), std::string(field_name)});
        }
        return make_expr(ft, lir::EFieldRead{std::move(recv), std::string(field_name)});
    }

    lir::LExprPtr lower_struct_lit(TinyMapView node) {
        auto sname = str_of(node.get(la::NAME.code));
        auto sit = structs_.find(std::string(sname));
        if (sit == structs_.end()) {
            error(std::format("struct literal: unknown struct '{}'", sname));
            return error_expr();
        }
        auto& sinfo = sit->second;

        // Lower all field values first (without validation), collecting names and types.
        std::vector<std::pair<std::string, lir::LExprPtr>> fields;
        if (node.has_key(la::ITEMS)) {
            auto inits = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < inits.size(); ++i) {
                auto init = map_of(inits.get(i));
                auto fname = str_of(init.get(la::NAME.code));
                lir::LExprPtr val = init.has_key(la::VALUE)
                    ? lower_expr(map_of(init.get(la::VALUE.code)))
                    : error_expr();
                fields.push_back({std::string(fname), std::move(val)});
            }
        }

        // For generic structs: infer type args and resolve to spec or template.
        if (!sinfo.type_params.empty()) {
            // Infer type args from field values against the generic template.
            SemaSubst inferred;
            for (auto& [fname, fval] : fields) {
                auto* raw_ft = field_type_of(std::string(sname), fname);
                if (!raw_ft) continue;
                if (raw_ft->kind == LogosType::Kind::TypeVar) {
                    auto& tv = raw_ft->type_var_name;
                    if (!inferred.count(tv)) {
                        auto* vt = fval->type;
                        if (vt->kind == LogosType::Kind::IntLit) vt = i32_t();
                        inferred[tv] = vt;
                    }
                }
            }
            std::vector<const LogosType*> args;
            for (auto& tp : sinfo.type_params) {
                auto it = inferred.find(tp.name);
                args.push_back(it != inferred.end() ? it->second : error_t());
            }
            const LogosType* lit_type = make_generic_struct(std::string(sname), args);

            // Check if a concrete specialization exists for these type args.
            std::string concrete = concrete_struct_name(lit_type);
            auto spec_it = struct_specs_sema_.find(concrete);
            const SemaStructInfo* effective = (spec_it != struct_specs_sema_.end())
                                              ? &spec_it->second : &sinfo;

            // Validate fields against the effective definition.
            std::unordered_map<std::string, bool> initialized;
            for (auto& f : effective->fields) initialized[std::string(f.name)] = false;
            for (auto& [fname, fval] : fields) {
                auto it = initialized.find(fname);
                if (it == initialized.end()) {
                    error(std::format("struct literal '{}': unknown field '{}'", sname, fname));
                } else {
                    it->second = true;
                    // Find field type in effective definition.
                    const LogosType* ft = nullptr;
                    for (auto& ef : effective->fields)
                        if (ef.name == fname) { ft = ef.type; break; }
                    if (ft && ft->kind != LogosType::Kind::Error &&
                        fval->type->kind != LogosType::Kind::Error &&
                        ft->kind != LogosType::Kind::TypeVar &&
                        !types_compatible(fval->type, ft)) {
                        error(std::format("struct literal '{}' field '{}': expected {}, got {}",
                              sname, fname, type_str(ft), type_str(fval->type)));
                    }
                }
            }
            for (auto& [fname, init] : initialized)
                if (!init)
                    error(std::format("struct literal '{}': field '{}' not initialized", sname, fname));

            return make_expr(lit_type, lir::EStructLit{std::string(sname), std::move(fields)});
        }

        // Non-generic struct: validate against template fields directly.
        std::unordered_map<std::string, bool> initialized;
        for (auto& f : sinfo.fields) initialized[std::string(f.name)] = false;
        for (auto& [fname, fval] : fields) {
            auto it = initialized.find(fname);
            if (it == initialized.end()) {
                error(std::format("struct literal '{}': unknown field '{}'", sname, fname));
            } else {
                it->second = true;
                auto* ft = field_type_of(std::string(sname), fname);
                if (ft && ft->kind != LogosType::Kind::Error &&
                    fval->type->kind != LogosType::Kind::Error &&
                    !types_compatible(fval->type, ft)) {
                    error(std::format("struct literal '{}' field '{}': expected {}, got {}",
                          sname, fname, type_str(ft), type_str(fval->type)));
                }
            }
        }
        for (auto& [fname, init] : initialized)
            if (!init)
                error(std::format("struct literal '{}': field '{}' not initialized", sname, fname));

        return make_expr(make_struct_type(std::string(sname)),
            lir::EStructLit{std::string(sname), std::move(fields)});
    }

    lir::LExprPtr lower_index_read(TinyMapView node) {
        auto recv = lower_expr(map_of(node.get(la::RECEIVER.code)));
        auto* arr_type = recv->type;

        if (arr_type->kind != LogosType::Kind::Array &&
            arr_type->kind != LogosType::Kind::Ptr &&
            arr_type->kind != LogosType::Kind::Error) {
            error(std::format("index read: receiver is not an array or pointer (got {})",
                  type_str(arr_type)));
        }

        lir::LExprPtr idx = node.has_key(la::VALUE)
            ? lower_expr(map_of(node.get(la::VALUE.code)))
            : error_expr();
        if (!is_integer(idx->type))
            error(std::format("array index must be integer, got {}", type_str(idx->type)));

        const LogosType* elem = error_t();
        if (arr_type->kind == LogosType::Kind::Array && arr_type->elem)  elem = arr_type->elem;
        if (arr_type->kind == LogosType::Kind::Ptr   && arr_type->pointee) elem = arr_type->pointee;

        return make_expr(elem, lir::EIndexRead{std::move(recv), std::move(idx)});
    }

    lir::LExprPtr lower_arr_lit(TinyMapView node) {
        if (!node.has_key(la::ITEMS)) {
            warn("empty array literal: element type unknown");
            return error_expr();
        }
        auto items = arr_of(node.get(la::ITEMS.code));
        if (items.size() == 0) {
            warn("empty array literal: element type unknown");
            return error_expr();
        }
        std::vector<lir::LExprPtr> elems;
        for (uint64_t i = 0; i < items.size(); ++i)
            elems.push_back(lower_expr(map_of(items.get(i))));

        const LogosType* elem_type = elems[0]->type;
        for (uint64_t i = 1; i < elems.size(); ++i) {
            auto* t = elems[i]->type;
            if (t->kind != LogosType::Kind::Error && elem_type->kind != LogosType::Kind::Error) {
                if (!types_compatible(t, elem_type) && !types_compatible(elem_type, t)) {
                    error(std::format("array literal: element {} has type {}, expected {}",
                          i, type_str(t), type_str(elem_type)));
                } else {
                    elem_type = unify_int(elem_type, t);
                }
            }
        }
        if (elem_type->kind == LogosType::Kind::IntLit) elem_type = i32_t();

        return make_expr(make_array(elem_type, elems.size()), lir::EArrLit{std::move(elems)});
    }

    lir::LExprPtr lower_enum_lit(TinyMapView node) {
        auto ename = str_of(node.get(la::NAME.code));
        auto vname = str_of(node.get(la::FIELD.code));
        auto eit = enums_.find(std::string(ename));
        if (eit == enums_.end()) {
            error(std::format("unknown enum '{}'", ename));
            return error_expr();
        }
        int32_t disc = 0;
        bool found = false;
        for (auto& v : eit->second.variants)
            if (v.name == vname) { disc = v.value; found = true; break; }
        if (!found) {
            error(std::format("enum '{}' has no variant '{}'", ename, vname));
            return error_expr();
        }
        return make_expr(make_enum_type(ename),
            lir::EEnumLit{std::string(ename), std::string(vname), disc});
    }

    // ── lower_stmt and friends ───────────────────────────────────

    lir::LStmt lower_stmt(TinyMapView stmt) {
        node_line_ = get_line(stmt);
        int32_t c = code_of(stmt);

        if (c == la::LET)          return lower_let(stmt);
        if (c == la::ASSIGN)       return lower_assign(stmt);
        if (c == la::RETURN)       return lower_return(stmt);
        if (c == la::IF)           return lower_if(stmt);
        if (c == la::WHILE)        return lower_while(stmt);
        if (c == la::FOR)          return lower_for(stmt);
        if (c == la::LOOP)         return lower_loop(stmt);
        if (c == la::FIELD_WRITE)  return lower_field_write(stmt);
        if (c == la::INDEX_WRITE)  return lower_index_write(stmt);
        if (c == la::MATCH)        return lower_match(stmt);
        if (c == la::EXPR_STMT) {
            lir::LExprPtr e = stmt.has_key(la::VALUE)
                ? lower_expr(map_of(stmt.get(la::VALUE.code)))
                : error_expr();
            return make_stmt(node_line_, lir::SExprStmt{std::move(e)});
        }
        if (c == la::BREAK) {
            if (loop_depth_ == 0) error("'break' outside loop");
            return make_stmt(node_line_, lir::SBreak{});
        }
        if (c == la::CONTINUE) {
            if (loop_depth_ == 0) error("'continue' outside loop");
            return make_stmt(node_line_, lir::SContinue{});
        }
        // Unknown stmt — emit dummy expr stmt
        return make_stmt(node_line_, lir::SExprStmt{error_expr()});
    }

    lir::LBlock lower_block(TinyMapView block) {
        lir::LBlock result;
        push_scope();
        if (block.has_key(la::ITEMS)) {
            auto stmts = arr_of(block.get(la::ITEMS.code));
            for (uint64_t i = 0; i < stmts.size(); ++i) {
                auto s = map_of(stmts.get(i));
                if (!s.is_null()) result.stmts.push_back(lower_stmt(s));
            }
        }
        pop_scope();
        return result;
    }

    lir::LStmt lower_let(TinyMapView node) {
        auto name = str_of(node.get(la::NAME.code));
        bool is_mut = false;
        if (node.has_key(la::IS_MUT)) {
            AnyVal av = node.get(la::IS_MUT.code);
            if (!av.is_null() && av.is_value()) is_mut = av.as_value<uint8_t>() != 0;
        }

        lir::LExprPtr rhs;
        const LogosType* rhs_type;
        if (node.has_key(la::VALUE)) {
            rhs      = lower_expr(map_of(node.get(la::VALUE.code)));
            rhs_type = rhs->type;
        } else {
            error(std::format("let '{}': missing value", name));
            rhs      = error_expr();
            rhs_type = error_t();
        }

        const LogosType* var_type;
        if (node.has_key(la::TYPE)) {
            auto* ann = resolve_type(map_of(node.get(la::TYPE.code)));
            if (ann->kind != LogosType::Kind::Error &&
                rhs_type->kind != LogosType::Kind::Error &&
                !types_compatible(rhs_type, ann)) {
                error(std::format("let '{}': type mismatch — expected {}, got {}",
                      name, type_str(ann), type_str(rhs_type)));
            }
            var_type = ann;
        } else {
            var_type = rhs_type;
            if (var_type->kind == LogosType::Kind::IntLit) var_type = i32_t();
        }

        define(name, var_type, is_mut);

        lir::SLet slet;
        slet.name   = std::string(name);
        slet.type   = var_type;
        slet.is_mut = is_mut;
        slet.value  = std::move(rhs);
        return make_stmt(node_line_, std::move(slet));
    }

    lir::LStmt lower_assign(TinyMapView node) {
        auto name = str_of(node.get(la::NAME.code));
        auto* var_type = lookup(name);
        if (!var_type) {
            error(std::format("assignment to undefined variable '{}'", name));
            lir::LExprPtr dummy = node.has_key(la::VALUE)
                ? lower_expr(map_of(node.get(la::VALUE.code)))
                : error_expr();
            return make_stmt(node_line_, lir::SAssign{std::string(name), std::move(dummy)});
        }
        if (!lookup_is_mut(name))
            error(std::format("assignment to immutable variable '{}'", name));

        lir::LExprPtr rhs = node.has_key(la::VALUE)
            ? lower_expr(map_of(node.get(la::VALUE.code)))
            : error_expr();
        if (var_type->kind != LogosType::Kind::Error &&
            rhs->type->kind != LogosType::Kind::Error &&
            !types_compatible(rhs->type, var_type)) {
            error(std::format("assignment to '{}': type mismatch — expected {}, got {}",
                  name, type_str(var_type), type_str(rhs->type)));
        }
        return make_stmt(node_line_, lir::SAssign{std::string(name), std::move(rhs)});
    }

    lir::LStmt lower_return(TinyMapView node) {
        lir::LExprPtr val;
        if (node.has_key(la::VALUE)) {
            AnyVal vav = node.get(la::VALUE.code);
            if (!vav.is_null()) {
                val = lower_expr(map_of(vav));
                if (ret_type_ && ret_type_->kind != LogosType::Kind::Error &&
                    val->type->kind != LogosType::Kind::Error &&
                    !types_compatible(val->type, ret_type_)) {
                    error(std::format("return type mismatch — expected {}, got {}",
                          type_str(ret_type_), type_str(val->type)));
                }
                return make_stmt(node_line_, lir::SReturn{std::move(val)});
            }
        }
        // void return
        if (ret_type_ && ret_type_->kind != LogosType::Kind::Void &&
            ret_type_->kind != LogosType::Kind::Error) {
            error(std::format("return without value in function returning {}",
                  type_str(ret_type_)));
        }
        return make_stmt(node_line_, lir::SReturn{nullptr});
    }

    lir::LStmt lower_if(TinyMapView node) {
        lir::LExprPtr cond;
        if (node.has_key(la::COND)) {
            cond = lower_expr(map_of(node.get(la::COND.code)));
            if (cond->type->kind != LogosType::Kind::Bool &&
                cond->type->kind != LogosType::Kind::Error)
                error(std::format("if condition must be bool, got {}", type_str(cond->type)));
        } else {
            cond = error_expr();
        }

        auto then_block = std::make_unique<lir::LBlock>();
        if (node.has_key(la::THEN))
            *then_block = lower_block(map_of(node.get(la::THEN.code)));

        std::optional<lir::LBlockPtr> else_opt;
        if (node.has_key(la::ELSE)) {
            auto else_node = map_of(node.get(la::ELSE.code));
            if (code_of(else_node) == la::BLOCK) {
                else_opt = std::make_unique<lir::LBlock>(lower_block(else_node));
            } else {
                // else if: wrap single SIf in a block
                auto inner_if = lower_if(else_node);
                auto b = std::make_unique<lir::LBlock>();
                b->stmts.push_back(std::move(inner_if));
                else_opt = std::move(b);
            }
        }

        lir::SIf sif;
        sif.cond  = std::move(cond);
        sif.then_ = std::move(then_block);
        sif.else_ = std::move(else_opt);
        return make_stmt(node_line_, std::move(sif));
    }

    lir::LStmt lower_while(TinyMapView node) {
        lir::LExprPtr cond;
        if (node.has_key(la::COND)) {
            cond = lower_expr(map_of(node.get(la::COND.code)));
            if (cond->type->kind != LogosType::Kind::Bool &&
                cond->type->kind != LogosType::Kind::Error)
                error(std::format("while condition must be bool, got {}", type_str(cond->type)));
        } else { cond = error_expr(); }

        auto body = std::make_unique<lir::LBlock>();
        if (node.has_key(la::BODY)) {
            ++loop_depth_;
            *body = lower_block(map_of(node.get(la::BODY.code)));
            --loop_depth_;
        }
        lir::SWhile sw; sw.cond = std::move(cond); sw.body = std::move(body);
        return make_stmt(node_line_, std::move(sw));
    }

    lir::LStmt lower_for(TinyMapView node) {
        auto var_name = str_of(node.get(la::NAME.code));

        lir::LExprPtr lo = node.has_key(la::LHS)
            ? lower_expr(map_of(node.get(la::LHS.code))) : error_expr();
        lir::LExprPtr hi = node.has_key(la::RHS)
            ? lower_expr(map_of(node.get(la::RHS.code))) : error_expr();

        if (!is_integer(lo->type) && lo->type->kind != LogosType::Kind::Error)
            error(std::format("for range start must be integer, got {}", type_str(lo->type)));
        if (!is_integer(hi->type) && hi->type->kind != LogosType::Kind::Error)
            error(std::format("for range end must be integer, got {}", type_str(hi->type)));

        bool inclusive = false;
        if (node.has_key(la::INCLUSIVE)) {
            AnyVal av = node.get(la::INCLUSIVE.code);
            if (!av.is_null() && av.is_value()) inclusive = av.as_value<uint8_t>() != 0;
        }

        push_scope();
        define(var_name, i32_t(), true);
        auto body = std::make_unique<lir::LBlock>();
        if (node.has_key(la::BODY)) {
            ++loop_depth_;
            *body = lower_block(map_of(node.get(la::BODY.code)));
            --loop_depth_;
        }
        pop_scope();

        lir::SFor sf;
        sf.var       = std::string(var_name);
        sf.lo        = std::move(lo);
        sf.hi        = std::move(hi);
        sf.inclusive = inclusive;
        sf.body      = std::move(body);
        return make_stmt(node_line_, std::move(sf));
    }

    lir::LStmt lower_loop(TinyMapView node) {
        auto body = std::make_unique<lir::LBlock>();
        if (node.has_key(la::BODY)) {
            ++loop_depth_;
            *body = lower_block(map_of(node.get(la::BODY.code)));
            --loop_depth_;
        }
        return make_stmt(node_line_, lir::SLoop{std::move(body)});
    }

    lir::LStmt lower_field_write(TinyMapView node) {
        auto recv_name  = str_of(node.get(la::RECEIVER.code));
        auto field_name = str_of(node.get(la::FIELD.code));
        auto sname = struct_name_of(recv_name);
        if (sname.empty()) {
            error(std::format("field write: '{}' is not a struct", recv_name));
        } else {
            auto* recv_type = lookup(recv_name);
            if (recv_type && recv_type->kind == LogosType::Kind::Ptr) {
                if (!recv_type->mut_ptr)
                    error(std::format("field write to '{}': receiver is *const pointer", recv_name));
            } else if (!lookup_is_mut(recv_name)) {
                error(std::format("field write to immutable variable '{}'", recv_name));
            }
        }
        const LogosType* recv_struct_t = sname.empty() ? nullptr : lookup(recv_name);
        if (recv_struct_t && recv_struct_t->kind == LogosType::Kind::Ptr)
            recv_struct_t = recv_struct_t->pointee;
        auto* ft = recv_struct_t ? field_type_of_for_type(recv_struct_t, field_name)
                                 : nullptr;
        if (!sname.empty() && !ft)
            error(std::format("field write: struct '{}' has no field '{}'", sname, field_name));

        lir::LExprPtr val = node.has_key(la::VALUE)
            ? lower_expr(map_of(node.get(la::VALUE.code)))
            : error_expr();
        if (ft && ft->kind != LogosType::Kind::Error &&
            val->type->kind != LogosType::Kind::Error &&
            !types_compatible(val->type, ft)) {
            error(std::format("field write '{}.{}': expected {}, got {}",
                  recv_name, field_name, type_str(ft), type_str(val->type)));
        }
        lir::SFieldWrite sfw;
        sfw.receiver = std::string(recv_name);
        sfw.field    = std::string(field_name);
        sfw.value    = std::move(val);
        return make_stmt(node_line_, std::move(sfw));
    }

    lir::LStmt lower_index_write(TinyMapView node) {
        auto arr_name = str_of(node.get(la::NAME.code));
        auto* arr_type = lookup(arr_name);
        if (!arr_type) {
            error(std::format("index write: undefined variable '{}'", arr_name));
        } else if (arr_type->kind != LogosType::Kind::Array &&
                   arr_type->kind != LogosType::Kind::Ptr &&
                   arr_type->kind != LogosType::Kind::Error) {
            error(std::format("index write: '{}' is not an array or pointer (got {})",
                  arr_name, type_str(arr_type)));
        } else if (arr_type->kind == LogosType::Kind::Array && !lookup_is_mut(arr_name)) {
            error(std::format("index write to immutable array '{}'", arr_name));
        } else if (arr_type->kind == LogosType::Kind::Ptr && !arr_type->mut_ptr) {
            error(std::format("index write through *const pointer '{}'", arr_name));
        }

        lir::LExprPtr idx = node.has_key(la::LHS)
            ? lower_expr(map_of(node.get(la::LHS.code))) : error_expr();
        if (!is_integer(idx->type))
            error(std::format("array index must be an integer, got {}", type_str(idx->type)));

        const LogosType* elem_type = nullptr;
        if (arr_type) {
            if (arr_type->kind == LogosType::Kind::Array) elem_type = arr_type->elem;
            else if (arr_type->kind == LogosType::Kind::Ptr) elem_type = arr_type->pointee;
        }

        lir::LExprPtr val = node.has_key(la::VALUE)
            ? lower_expr(map_of(node.get(la::VALUE.code))) : error_expr();
        if (elem_type && elem_type->kind != LogosType::Kind::Error &&
            val->type->kind != LogosType::Kind::Error &&
            !types_compatible(val->type, elem_type)) {
            error(std::format("index write to '{}': expected {}, got {}",
                  arr_name, type_str(elem_type), type_str(val->type)));
        }

        lir::SIndexWrite siw;
        siw.arr   = std::string(arr_name);
        siw.index = std::move(idx);
        siw.value = std::move(val);
        return make_stmt(node_line_, std::move(siw));
    }

    lir::LStmt lower_match(TinyMapView node) {
        lir::LExprPtr scrut;
        const LogosType* scrut_type = error_t();
        if (node.has_key(la::VALUE)) {
            scrut = lower_expr(map_of(node.get(la::VALUE.code)));
            scrut_type = scrut->type;
        } else { scrut = error_expr(); }

        lir::SMatch smatch;
        smatch.scrut = std::move(scrut);

        if (node.has_key(la::ITEMS)) {
            auto arms = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < arms.size(); ++i) {
                auto arm = map_of(arms.get(i));
                if (code_of(arm) != la::MATCH_ARM) continue;

                // Build pattern
                lir::Pattern pat = lir::PatWild{"_"};
                if (arm.has_key(la::LHS)) {
                    auto pnode = map_of(arm.get(la::LHS.code));
                    int32_t pc = code_of(pnode);
                    if (pc == la::PAT_VARIANT) {
                        auto pename = std::string(str_of(pnode.get(la::NAME.code)));
                        auto pvname = std::string(str_of(pnode.get(la::FIELD.code)));
                        int32_t disc = 0;
                        auto eit = enums_.find(pename);
                        if (eit == enums_.end()) {
                            error(std::format("match pattern: unknown enum '{}'", pename));
                        } else {
                            if (scrut_type->kind == LogosType::Kind::Enum &&
                                scrut_type->enum_name != pename)
                                error(std::format("match: enum '{}' != scrutinee '{}'",
                                      pename, type_str(scrut_type)));
                            bool found = false;
                            for (auto& v : eit->second.variants)
                                if (v.name == pvname) { disc = v.value; found = true; break; }
                            if (!found)
                                error(std::format("match: enum '{}' has no variant '{}'",
                                      pename, pvname));
                        }
                        pat = lir::PatVariant{pename, pvname, disc};
                    } else if (pc == la::PAT_INT) {
                        auto sv = str_of(pnode.get(la::VALUE.code));
                        pat = lir::PatInt{(int32_t)std::strtol(sv.data(), nullptr, 10)};
                    } else if (pc == la::PAT_BOOL) {
                        AnyVal bv = pnode.get(la::VALUE.code);
                        bool bval = !bv.is_null() && bv.is_value() && bv.as_value<uint8_t>();
                        pat = lir::PatBool{bval};
                    } else if (pc == la::PAT_WILD) {
                        auto wname = str_of(pnode.get(la::NAME.code));
                        pat = lir::PatWild{std::string(wname)};
                    }
                }

                // Build body block
                lir::LBlockPtr body = std::make_unique<lir::LBlock>();
                if (arm.has_key(la::BODY)) {
                    auto body_node = map_of(arm.get(la::BODY.code));
                    if (code_of(body_node) == la::BLOCK) {
                        *body = lower_block(body_node);
                    } else {
                        body->stmts.push_back(lower_stmt(body_node));
                    }
                }
                smatch.arms.push_back({std::move(pat), std::move(body)});
            }
        }
        return make_stmt(node_line_, std::move(smatch));
    }

    // ── lower_fn ─────────────────────────────────────────────────

    lir::LFunction lower_fn(TinyMapView node, std::string_view struct_ctx = {}) {
        auto raw_name = str_of(node.get(la::NAME.code));
        std::string mangled = struct_ctx.empty()
            ? std::string(raw_name)
            : std::string(struct_ctx) + "__" + std::string(raw_name);

        ctx_       = std::format("fn {}", mangled);
        node_line_ = get_line(node);

        lir::LFunction fn;
        fn.name      = mangled;
        fn.is_extern = (code_of(node) == la::EXTERN_FN);

        auto fit = funcs_.find(mangled);
        if (fit == funcs_.end()) return fn;   // shouldn't happen after collect

        fn.type_params = fit->second.type_params;
        fn.ret_type    = fit->second.ret_type;
        ret_type_      = fn.ret_type;

        // Put type params in scope for the duration of the function body
        push_type_params(fn.type_params);

        scope_.clear();
        push_scope();

        // Parameters
        if (node.has_key(la::PARAMS)) {
            auto params_av = node.get(la::PARAMS.code);
            if (params_av.is_pointer()) {
                auto params_node = map_of(params_av);
                if (params_node.has_key(la::ITEMS)) {
                    auto arr = arr_of(params_node.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < arr.size(); ++i) {
                        auto p = map_of(arr.get(i));
                        if (code_of(p) != la::PARAM) continue;
                        auto pname = str_of(p.get(la::NAME.code));
                        auto ptype = fit->second.param_types[i];
                        define(pname, ptype);
                        fn.params.push_back({std::string(pname), ptype});
                    }
                }
            }
        }

        // Body (extern fns have no body)
        if (!fn.is_extern && node.has_key(la::BODY)) {
            auto body_node = map_of(node.get(la::BODY.code));
            fn.body = lower_block(body_node);
            // Return reachability check (on AST node — before scope is gone)
            if (fn.ret_type && fn.ret_type->kind != LogosType::Kind::Void &&
                fn.ret_type->kind != LogosType::Kind::Error &&
                !block_always_returns(body_node)) {
                error("not all paths return a value");
            }
        }

        pop_scope();
        pop_type_params(fn.type_params);
        return fn;
    }

    // ── lower_struct/enum/const/alias ────────────────────────────

    lir::LStructDef lower_struct_def(TinyMapView node) {
        auto sname = std::string(str_of(node.get(la::NAME.code)));
        lir::LStructDef sd;
        sd.name = sname;
        auto& sinfo = structs_[sname];
        sd.type_params = sinfo.type_params;
        push_type_params(sd.type_params);
        for (auto& f : sinfo.fields)
            sd.fields.push_back({std::string(f.name), f.type});
        if (node.has_key(la::ITEMS)) {
            auto methods = arr_of(node.get(la::ITEMS.code));
            for (uint64_t m = 0; m < methods.size(); ++m) {
                auto method = map_of(methods.get(m));
                if (code_of(method) == la::FN)
                    sd.methods.push_back(lower_fn(method, sname));
            }
        }
        pop_type_params(sd.type_params);
        return sd;
    }

    lir::LEnumDef lower_enum_def(TinyMapView node) {
        auto ename = std::string(str_of(node.get(la::NAME.code)));
        lir::LEnumDef ed;
        ed.name = ename;
        auto& einfo = enums_[ename];
        for (auto& v : einfo.variants)
            ed.variants.push_back({std::string(v.name), v.value});
        return ed;
    }

    lir::LConst lower_const_def(TinyMapView node) {
        auto name = std::string(str_of(node.get(la::NAME.code)));
        lir::LConst lc;
        lc.name = name;
        auto cit = module_consts_.find(name);
        lc.type = (cit != module_consts_.end()) ? cit->second : error_t();
        if (node.has_key(la::VALUE))
            lc.value = lower_expr(map_of(node.get(la::VALUE.code)));
        else
            lc.value = error_expr();
        return lc;
    }

    lir::LTypeAlias lower_type_alias_def(TinyMapView node) {
        auto name = std::string(str_of(node.get(la::NAME.code)));
        lir::LTypeAlias ta;
        ta.name = name;
        auto ait = type_aliases_.find(name);
        ta.type = (ait != type_aliases_.end()) ? ait->second : error_t();
        return ta;
    }

    // ── lower_program ────────────────────────────────────────────

    void lower_program(const std::vector<hermes::HermesCtr>& asts, lir::LProgram& prog) {
        for (size_t i = 0; i < asts.size(); ++i) {
            holder_ = asts[i].holder();
            file_ = (filenames_ && i < filenames_->size()) ? (*filenames_)[i] : std::string{};
            auto root = asts[i].root_object().as_tiny_map();
            lower_module_items(root, prog);
        }
    }

    void lower_module_items(TinyMapView mod, lir::LProgram& prog) {
        if (!mod.has_key(la::ITEMS)) return;
        auto items = arr_of(mod.get(la::ITEMS.code));
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto item = map_of(items.get(i));
            int32_t c = code_of(item);
            if      (c == la::STRUCT) {
                if (is_specialization_struct(item))
                    prog.struct_specializations.push_back(lower_spec_struct(item));
                else
                    prog.structs.push_back(lower_struct_def(item));
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
        }
    }
};

} // anonymous namespace

lir::LProgram sema_lower(const std::vector<logos::hermes::HermesCtr>& asts,
                          const std::vector<std::string>& filenames) {
    SemaChecker checker;
    return checker.run(asts, filenames);
}

} // namespace logos::compiler
