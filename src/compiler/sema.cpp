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
#include <unordered_set>
#include <vector>
#include <functional>

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
    case LogosType::Kind::Class:
        return a.struct_name == b.struct_name;
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

std::string concrete_class_name(const LogosType* t) {
    if (!t || t->kind != LogosType::Kind::Class) return {};
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
    case LogosType::Kind::Class:
        return concrete_class_name(t);
    case LogosType::Kind::Tuple: {
        std::string r = "tup";
        for (auto* e : t->tuple_elems) { r += "_"; r += mangle_type_for_name(e); }
        return r;
    }
    case LogosType::Kind::Slice:
        return "slice_" + mangle_type_for_name(t->elem);
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
    // *const u8 → &[u8] (string literal to str coercion)
    if (from->kind == LogosType::Kind::Ptr && to->kind == LogosType::Kind::Slice &&
        from->pointee && to->elem &&
        from->pointee->kind == LogosType::Kind::U8 && to->elem->kind == LogosType::Kind::U8)
        return true;
    // Tuple: element-wise compatibility (e.g. ({integer}, {integer}) → (i32, i32))
    if (from->kind == LogosType::Kind::Tuple && to->kind == LogosType::Kind::Tuple) {
        if (from->tuple_elems.size() != to->tuple_elems.size()) return false;
        for (size_t i = 0; i < from->tuple_elems.size(); ++i)
            if (!types_compatible(from->tuple_elems[i], to->tuple_elems[i])) return false;
        return true;
    }
    // Class pointer covariance: *mut/const Derived is compatible with *const/mut Class
    // (same class — exact equality handled above; hierarchy checked in SemaChecker::compat)
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
    case LogosType::Kind::Class:
        if (t->type_args.empty()) return t->struct_name;
        { std::string r = t->struct_name + "<";
          for (size_t i = 0; i < t->type_args.size(); ++i) {
              if (i) r += ", ";
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

    // prims_[int(Kind)] for primitive kinds.  Class and TypeVar are not primitives.
    // Size = int(Kind::Error) + 1 to cover all Kind values.
    std::array<const LogosType*, int(LogosType::Kind::Error) + 1> prims_{};

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
    const LogosType* make_class_type(std::string_view name) {
        LogosType t; t.kind = LogosType::Kind::Class; t.struct_name = std::string(name);
        return pool_.alloc(std::move(t));
    }
    const LogosType* make_generic_struct(std::string_view name,
                                          std::vector<const LogosType*> args) {
        LogosType t; t.kind = LogosType::Kind::Struct;
        t.struct_name = std::string(name);
        t.type_args   = std::move(args);
        return pool_.alloc(std::move(t));
    }
    const LogosType* make_generic_class(std::string_view name,
                                         std::vector<const LogosType*> args) {
        LogosType t; t.kind = LogosType::Kind::Class;
        t.struct_name = std::string(name);   // struct_name holds class name
        t.type_args   = std::move(args);
        return pool_.alloc(std::move(t));
    }
    const LogosType* make_enum_type(std::string_view name) {
        LogosType t; t.kind = LogosType::Kind::Enum; t.enum_name = name;
        return pool_.alloc(std::move(t));
    }
    const LogosType* make_tuple_type(std::vector<const LogosType*> elems) {
        LogosType t; t.kind = LogosType::Kind::Tuple;
        t.tuple_elems = std::move(elems);
        return pool_.alloc(std::move(t));
    }
    const LogosType* make_closure_type(std::vector<const LogosType*> params, const LogosType* ret) {
        LogosType t; t.kind = LogosType::Kind::Closure;
        t.closure_params = std::move(params);
        t.closure_ret = ret;
        return pool_.alloc(std::move(t));
    }
    const LogosType* make_slice_type(const LogosType* elem) {
        LogosType t; t.kind = LogosType::Kind::Slice;
        t.elem = elem;
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
    int         closure_counter_ = 0;

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

    // Returns class name (base name) if the var is a class or *class.
    std::string_view class_name_of(std::string_view var_name) {
        auto* t = lookup(var_name);
        if (!t) return {};
        if (t->kind == LogosType::Kind::Class) return t->struct_name;
        if (t->kind == LogosType::Kind::Ptr && t->pointee &&
            t->pointee->kind == LogosType::Kind::Class)
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

    // Returns class name if the type is a class or a pointer to a class.
    std::string_view class_name_from_type(const LogosType* t) {
        if (!t) return {};
        if (t->kind == LogosType::Kind::Class) return t->struct_name;
        if (t->kind == LogosType::Kind::Ptr && t->pointee &&
            t->pointee->kind == LogosType::Kind::Class)
            return t->pointee->struct_name;
        return {};
    }

    // Check if `derived` is the same as or a subclass of `base`.
    bool is_subclass(std::string_view derived, std::string_view base) const {
        if (derived == base) return true;
        auto it = classes_.find(std::string(derived));
        if (it == classes_.end()) return false;
        if (it->second.parent_name.empty()) return false;
        return is_subclass(it->second.parent_name, base);
    }

    // Field type for a class (walks the all_fields list).
    const LogosType* class_field_type(std::string_view cname, std::string_view fname) const {
        auto it = classes_.find(std::string(cname));
        if (it == classes_.end()) return nullptr;
        for (auto& [fn, ft] : it->second.all_fields)
            if (fn == fname) return ft;
        return nullptr;
    }

    // vtable index of a method in a class (returns -1 if not found).
    int32_t vtable_index_of(std::string_view cname, std::string_view mangled_method) const {
        auto it = classes_.find(std::string(cname));
        if (it == classes_.end()) return -1;
        auto& order = it->second.vtable_order;
        for (int32_t i = 0; i < (int32_t)order.size(); ++i)
            if (order[i] == mangled_method) return i;
        return -1;
    }

    // ── Module-level symbol tables ───────────────────────────────

    struct SemaFieldInfo  { std::string_view name; const LogosType* type; };
    struct SemaStructInfo { std::vector<SemaFieldInfo> fields; std::vector<TypeParam> type_params; };
    struct SemaFuncInfo   { std::vector<const LogosType*> param_types; const LogosType* ret_type;
                            std::vector<TypeParam> type_params; bool is_vararg = false; };
    struct SemaVariantInfo{
        std::string_view name; int32_t value;
        std::vector<const LogosType*> payload_types;  // empty = no payload
    };
    struct SemaEnumInfo   {
        std::vector<SemaVariantInfo> variants;
        std::vector<TypeParam> type_params;  // for generic enums
    };
    struct SemaClassInfo  {
        std::string parent_name;
        std::vector<const LogosType*> parent_type_args;  // type args passed to parent (e.g. [TypeVar(T)])
        bool is_abstract = false;
        // All fields accessible on this class (parent fields first, then own).
        std::vector<std::pair<std::string, const LogosType*>> all_fields;
        // vtable_order: full vtable including inherited slots (parent methods first).
        // Each entry is the mangled method name, e.g. "Animal__speak".
        std::vector<std::string> vtable_order;
        // Generic class support: non-empty when class has type parameters.
        std::vector<TypeParam> type_params;
    };

    // ── Trait info ───────────────────────────────────────────────
    struct SemaTraitMethodInfo {
        std::string name;
        std::vector<const LogosType*> param_types;  // includes self
        const LogosType* ret_type = nullptr;
    };
    struct SemaTraitInfo {
        std::string name;
        std::vector<SemaTraitMethodInfo> methods;
    };
    struct SemaImplInfo {
        std::string trait_name;
        std::string target_type;
    };

    // Type params in scope for the function/struct currently being processed.
    // Maps type param name → TypeVar LogosType*.
    std::unordered_map<std::string, const LogosType*> current_type_params_;

    std::unordered_map<std::string, SemaStructInfo>   structs_;
    // concrete_name (e.g. "Pair__i32") → SemaStructInfo for explicit specializations.
    std::unordered_map<std::string, SemaStructInfo>   struct_specs_sema_;
    std::unordered_map<std::string, SemaEnumInfo>     enums_;
    std::unordered_map<std::string, SemaClassInfo>    classes_;
    std::unordered_map<std::string, SemaFuncInfo>     funcs_;
    std::unordered_map<std::string, const LogosType*> type_aliases_;
    std::unordered_map<std::string, const LogosType*> module_consts_;
    std::unordered_map<std::string, SemaTraitInfo>    traits_;
    // "TraitName::TypeName" → impl info
    std::unordered_map<std::string, SemaImplInfo>     impls_;

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
        case LogosType::Kind::Tuple: {
            std::vector<const LogosType*> new_elems;
            bool changed = false;
            for (auto* e : t->tuple_elems) {
                auto* ne = subst_type_sema(e, s);
                changed |= (ne != e);
                new_elems.push_back(ne);
            }
            if (!changed) return t;
            return make_tuple_type(std::move(new_elems));
        }
        case LogosType::Kind::Slice: {
            auto* elem = subst_type_sema(t->elem, s);
            if (elem == t->elem) return t;
            return make_slice_type(elem);
        }
        default: return t;
        }
    }

    // ── Compatibility with class hierarchy ───────────────────────
    // Use this inside SemaChecker instead of static types_compatible when
    // class pointer upcasting should be allowed.
    bool compat(const LogosType* from, const LogosType* to) const {
        if (types_compatible(from, to)) return true;
        // *mut/const Derived compatible with *const/mut Base (upcast)
        if (from && to &&
            from->kind == LogosType::Kind::Ptr && to->kind == LogosType::Kind::Ptr &&
            from->pointee && to->pointee &&
            from->pointee->kind == LogosType::Kind::Class &&
            to->pointee->kind   == LogosType::Kind::Class) {
            return is_subclass(from->pointee->struct_name, to->pointee->struct_name);
        }
        return false;
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

        if (tc == la::SLICE_TYPE) {
            auto* elem = node.has_key(la::TYPE)
                ? resolve_type(map_of(node.get(la::TYPE.code)))
                : error_t();
            return make_slice_type(elem);
        }

        if (tc == la::TUPLE_TYPE) {
            std::vector<const LogosType*> elems;
            if (node.has_key(la::ITEMS)) {
                auto items = arr_of(node.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i)
                    elems.push_back(resolve_type(map_of(items.get(i))));
            }
            return make_tuple_type(std::move(elems));
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
            if (name == "str")  return make_slice_type(u8_t());
            // Check if it's a type variable in scope
            auto tvit = current_type_params_.find(std::string(name));
            if (tvit != current_type_params_.end()) return tvit->second;
            auto ait = type_aliases_.find(std::string(name));
            if (ait != type_aliases_.end()) return ait->second;
            if (structs_.count(std::string(name))) return make_struct_type(name);
            if (classes_.count(std::string(name))) return make_class_type(name);
            if (enums_.count(std::string(name)))   return make_enum_type(name);
            error(std::format("unknown type '{}'", name));
            return error_t();
        }

        if (tc == la::GENERIC_INST) {
            auto name = str_of(node.get(la::NAME.code));
            bool is_struct = structs_.count(std::string(name)) > 0;
            bool is_class  = classes_.count(std::string(name)) > 0;
            bool is_enum   = enums_.count(std::string(name)) > 0;
            if (!is_struct && !is_class && !is_enum) {
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
            if (is_enum) {
                LogosType t; t.kind = LogosType::Kind::Enum;
                t.enum_name = std::string(name);
                t.type_args = std::move(args);
                return pool_.alloc(std::move(t));
            }
            if (is_class) {
                if (args.empty()) return make_class_type(name);
                return make_generic_class(name, std::move(args));
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
                } else if (ic == la::CLASS) {
                    auto cname = std::string(str_of(item.get(la::NAME.code)));
                    if (classes_.count(cname)) error(std::format("duplicate class '{}'", cname));
                    else classes_[cname] = {};
                }
            }
        }
        // Second pass: fill in fields, variants, function signatures.
        for (auto& ast : asts) {
            holder_ = ast.holder();
            auto root = ast.root_object().as_tiny_map();
            collect_module(root);
        }

        // Third pass: build class inheritance (all_fields + full vtable_order).
        finalize_classes();
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
            else if (c == la::CLASS)                        collect_class(item);
            else if (c == la::FN || c == la::EXTERN_FN)   collect_fn(item);
            else if (c == la::TYPE_ALIAS)                 collect_type_alias(item);
            else if (c == la::CONST_DEF)                  collect_const(item);
            else if (c == la::TRAIT_DEF)                  collect_trait(item);
            else if (c == la::IMPL_BLOCK)                 collect_impl(item);
        }
    }

    void collect_enum(TinyMapView node) {
        auto ename = std::string(str_of(node.get(la::NAME.code)));
        ctx_ = std::format("enum {}", ename);
        SemaEnumInfo info;
        info.type_params = read_type_params(node);
        push_type_params(info.type_params);
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
                        // Read payload types for tagged union variants
                        std::vector<const LogosType*> payload;
                        if (v.has_key(la::ITEMS)) {
                            auto pitems = arr_of(v.get(la::ITEMS.code));
                            for (uint64_t j = 0; j < pitems.size(); ++j)
                                payload.push_back(resolve_type(map_of(pitems.get(j))));
                        }
                        info.variants.push_back({vname, vval, std::move(payload)});
                        next_val = vval + 1;
                    }
                }
            }
        }
        pop_type_params(info.type_params);
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

    void collect_trait(TinyMapView node) {
        auto tname = std::string(str_of(node.get(la::NAME.code)));
        ctx_ = std::format("trait {}", tname);
        // Push "Self" as a type param so trait method signatures can reference it.
        current_type_params_["Self"] = make_typevar("Self");
        SemaTraitInfo info;
        info.name = tname;
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto m = map_of(items.get(i));
                if (code_of(m) != la::FN) continue;
                SemaTraitMethodInfo mi;
                mi.name = std::string(str_of(m.get(la::NAME.code)));
                if (m.has_key(la::PARAMS)) {
                    auto pav = m.get(la::PARAMS.code);
                    if (!pav.is_null() && pav.is_pointer()) {
                        auto plist = map_of(pav);
                        if (plist.has_key(la::ITEMS)) {
                            auto params = arr_of(plist.get(la::ITEMS.code));
                            for (uint64_t j = 0; j < params.size(); ++j) {
                                auto p = map_of(params.get(j));
                                if (p.has_key(la::TYPE))
                                    mi.param_types.push_back(resolve_type(map_of(p.get(la::TYPE.code))));
                            }
                        }
                    }
                }
                mi.ret_type = m.has_key(la::RET_TYPE)
                    ? resolve_type(map_of(m.get(la::RET_TYPE.code))) : void_t();
                info.methods.push_back(std::move(mi));
            }
        }
        current_type_params_.erase("Self");
        traits_[tname] = std::move(info);
    }

    void collect_impl(TinyMapView node) {
        std::string trait_name;
        if (node.has_key(la::NAME))
            trait_name = std::string(str_of(node.get(la::NAME.code)));
        // TYPE is the target type (simple_type: IDENT or GENERIC_INST)
        std::string target;
        if (node.has_key(la::TYPE)) {
            auto tnode = map_of(node.get(la::TYPE.code));
            target = std::string(str_of(tnode.get(la::NAME.code)));
        }
        if (trait_name.empty())
            ctx_ = std::format("impl {}", target);
        else
            ctx_ = std::format("impl {} for {}", trait_name, target);
        // Verify trait exists (only for trait impls)
        if (!trait_name.empty() && !traits_.count(trait_name))
            error(std::format("impl: unknown trait '{}'", trait_name));
        // Register impl methods as free functions with mangled names: Target__method
        // Skip if already registered (e.g. class methods defined inline).
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto m = map_of(items.get(i));
                if (code_of(m) == la::FN || code_of(m) == la::STATIC_FN) {
                    auto mname = std::string(str_of(m.get(la::NAME.code)));
                    auto mangled = target + "__" + mname;
                    if (!funcs_.count(mangled))
                        collect_fn(m, target);
                }
            }
        }
        // Check completeness: every required trait method must be in the impl.
        if (!trait_name.empty()) {
            auto tit = traits_.find(trait_name);
            if (tit != traits_.end()) {
                for (auto& m : tit->second.methods) {
                    auto mangled = target + "__" + m.name;
                    if (!funcs_.count(mangled))
                        error(std::format("impl {} for {}: missing method '{}'",
                              trait_name, target, m.name));
                }
            }
        }
        // Register the impl mapping (only for trait impls)
        if (!trait_name.empty())
            impls_[trait_name + "::" + target] = {trait_name, target};
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

    // ── collect_class ─────────────────────────────────────────────
    // Phase 1: register own fields, method signatures, parent name, is_abstract.
    // Phase 2 (finalize_classes): build all_fields and full vtable_order.
    void collect_class(TinyMapView node) {
        auto cname = std::string(str_of(node.get(la::NAME.code)));
        ctx_ = std::format("class {}", cname);

        SemaClassInfo& info = classes_[cname];

        // Read type parameters (generic classes: class Box<T> { ... })
        info.type_params = read_type_params(node);
        push_type_params(info.type_params);

        // Read IS_ABSTRACT flag
        if (node.has_key(la::IS_ABSTRACT)) {
            AnyVal av = node.get(la::IS_ABSTRACT.code);
            info.is_abstract = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
        }

        // Read parent class
        if (node.has_key(la::PARENT)) {
            auto parent_av = node.get(la::PARENT.code);
            // PARENT is now a simple_type node: TYPE_REF or GENERIC_INST
            auto parent_node = map_of(parent_av);
            info.parent_name = std::string(str_of(parent_node.get(la::NAME.code)));
            // Read parent type args if generic (e.g. extends Container<T>)
            if (code_of(parent_node) == la::GENERIC_INST && parent_node.has_key(la::ITEMS)) {
                auto items = arr_of(parent_node.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i)
                    info.parent_type_args.push_back(resolve_type(map_of(items.get(i))));
            }
            if (!classes_.count(info.parent_name))
                error(std::format("class '{}': unknown parent '{}'", cname, info.parent_name));
        }

        // Process class members: collect own fields and method signatures
        if (node.has_key(la::ITEMS)) {
            auto members = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < members.size(); ++i) {
                auto m = map_of(members.get(i));
                int32_t mc = code_of(m);

                if (mc == la::FIELD_DEF) {
                    auto fname = str_of(m.get(la::NAME.code));
                    auto ftype = resolve_type(map_of(m.get(la::TYPE.code)));
                    // Own fields (all_fields built in finalize_classes)
                    info.all_fields.push_back({std::string(fname), ftype});

                } else if (mc == la::FN || mc == la::ABSTRACT_FN) {
                    collect_fn(m, cname);
                } else if (mc == la::STATIC_FN) {
                    // Static methods: collected as free functions with class prefix
                    collect_fn(m, cname);
                }
            }
        }

        pop_type_params(classes_[cname].type_params);
    }

    // Phase 2: walk class hierarchy (assumes parent before child) to build
    // all_fields (parent fields first) and full vtable_order.
    void finalize_classes() {
        // We process in order they appear in classes_ — but to correctly inherit,
        // we do a simple recursive helper that resolves each class at most once.
        std::unordered_map<std::string, bool> done;

        std::function<void(const std::string&)> resolve = [&](const std::string& cname) {
            if (done.count(cname)) return;
            done[cname] = true;

            auto it = classes_.find(cname);
            if (it == classes_.end()) return;
            auto& info = it->second;

            // Resolve parent first
            if (!info.parent_name.empty()) {
                resolve(info.parent_name);
                auto pit = classes_.find(info.parent_name);
                if (pit != classes_.end()) {
                    // Prepend parent's all_fields to own
                    auto own_fields = std::move(info.all_fields);
                    info.all_fields = pit->second.all_fields;
                    for (auto& f : own_fields) info.all_fields.push_back(f);
                    // Start vtable from parent
                    info.vtable_order = pit->second.vtable_order;
                }
            }

            // Add own methods to vtable (skip if already present = override)
            for (auto& [fname, finfo] : funcs_) {
                // Check if this method belongs to cname
                std::string prefix = cname + "__";
                if (fname.rfind(prefix, 0) != 0) continue;
                auto mangled = fname;
                auto vit = std::find(info.vtable_order.begin(), info.vtable_order.end(), mangled);
                if (vit == info.vtable_order.end())
                    info.vtable_order.push_back(mangled);
            }
        };

        for (auto& [cname, _] : classes_)
            resolve(cname);
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
        structs_[sname] = std::move(info);
        // Methods must be collected with the struct's type params in scope.
        if (node.has_key(la::ITEMS)) {
            auto methods = arr_of(node.get(la::ITEMS.code));
            for (uint64_t m = 0; m < methods.size(); ++m) {
                auto method = map_of(methods.get(m));
                int32_t mc = code_of(method);
                if (mc == la::FN || mc == la::STATIC_FN) collect_fn(method, sname);
            }
        }
        pop_type_params(structs_[sname].type_params);
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
        // Vararg flag (for extern fn with ... params)
        if (node.has_key(la::IS_VARARG)) {
            AnyVal av = node.get(la::IS_VARARG.code);
            info.is_vararg = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
        }
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
            bool all_ret = true;
            for (uint64_t i = 0; i < arms.size(); ++i) {
                auto arm = map_of(arms.get(i));
                if (code_of(arm) != la::MATCH_ARM) continue;
                if (arm.has_key(la::BODY)) {
                    auto body = map_of(arm.get(la::BODY.code));
                    bool arm_ret = (code_of(body) == la::BLOCK)
                                   ? block_always_returns(body)
                                   : stmt_always_returns(body);
                    if (!arm_ret) all_ret = false;
                } else { all_ret = false; }
            }
            // Match always returns if all arms return. This covers both the
            // classic wildcard-arm case and exhaustive enum matches without _.
            // Guard against empty arm list (all_ret stays true vacuously).
            return all_ret && arms.size() > 0;
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
        case la::STATIC_CALL:  return lower_static_call(expr);
        case la::FIELD_READ:  return lower_field_read(expr);
        case la::STRUCT_LIT:  return lower_struct_lit(expr);
        case la::INDEX_READ:  return lower_index_read(expr);
        case la::ARR_LIT:     return lower_arr_lit(expr);
        case la::ENUM_LIT:    return lower_enum_lit(expr);
        case la::ENUM_LIT_DATA: return lower_enum_lit_data(expr);
        case la::NEW_EXPR:    return lower_new_expr(expr);
        case la::IF:          return lower_if_expr(expr);
        case la::CLOSURE_EXPR: return lower_closure_expr(expr);

        case la::TUPLE_LIT: {
            if (!expr.has_key(la::ITEMS)) return error_expr();
            auto items = arr_of(expr.get(la::ITEMS.code));
            std::vector<lir::LExprPtr> elems;
            std::vector<const LogosType*> elem_types;
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto e = lower_expr(map_of(items.get(i)));
                elem_types.push_back(e->type);
                elems.push_back(std::move(e));
            }
            auto* tt = make_tuple_type(std::move(elem_types));
            return make_expr(tt, lir::ETupleLit{std::move(elems)});
        }

        case la::TUPLE_INDEX: {
            auto recv = expr.has_key(la::RECEIVER)
                ? lower_expr(map_of(expr.get(la::RECEIVER.code)))
                : error_expr();
            if (recv->type->kind != LogosType::Kind::Tuple) {
                error(std::format("tuple index on non-tuple type '{}'", type_str(recv->type)));
                return error_expr();
            }
            auto sv = str_of(expr.get(la::FIELD.code));
            uint32_t idx = (uint32_t)std::strtoul(sv.data(), nullptr, 10);
            if (idx >= recv->type->tuple_elems.size()) {
                error(std::format("tuple index {} out of range (tuple has {} elements)",
                      idx, recv->type->tuple_elems.size()));
                return error_expr();
            }
            auto* elem_t = recv->type->tuple_elems[idx];
            return make_expr(elem_t, lir::ETupleIndex{std::move(recv), idx});
        }

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

        // Operator overloading: if LHS is a struct/class, desugar to trait method call.
        if (lt->kind == LogosType::Kind::Struct || lt->kind == LogosType::Kind::Class) {
            // Map operator to trait name and method
            std::string trait_name, method_name;
            if      (op == "+")  { trait_name = "Add"; method_name = "add"; }
            else if (op == "-")  { trait_name = "Sub"; method_name = "sub"; }
            else if (op == "*")  { trait_name = "Mul"; method_name = "mul"; }
            else if (op == "/")  { trait_name = "Div"; method_name = "div"; }
            else if (op == "%")  { trait_name = "Rem"; method_name = "rem"; }
            else if (op == "==") { trait_name = "Eq";  method_name = "eq"; }
            else if (op == "!=") { trait_name = "Eq";  method_name = "ne"; }
            else if (op == "<")  { trait_name = "Ord"; method_name = "lt"; }
            else if (op == "<=") { trait_name = "Ord"; method_name = "le"; }
            else if (op == ">")  { trait_name = "Ord"; method_name = "gt"; }
            else if (op == ">=") { trait_name = "Ord"; method_name = "ge"; }
            if (!trait_name.empty()) {
                auto type_name = (lt->kind == LogosType::Kind::Struct)
                    ? concrete_struct_name(lt) : concrete_class_name(lt);
                auto mangled = type_name + "__" + method_name;
                auto fit = funcs_.find(mangled);
                if (fit != funcs_.end()) {
                    std::vector<lir::LExprPtr> args;
                    args.push_back(std::move(lhs));
                    args.push_back(std::move(rhs));
                    return make_expr(fit->second.ret_type,
                        lir::ECall{mangled, {}, std::move(args)});
                }
                // No impl found — fall through to normal type checking
            }
        }

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

        // & — address-of or array-to-slice
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
            // &array → slice: &[T] with len = array size
            if (vt->kind == LogosType::Kind::Array) {
                auto addr = make_expr(make_ptr(false, vt->elem), lir::EAddrOf{std::string(var_name)});
                auto len  = make_expr(prim(LogosType::Kind::I64), lir::ELitInt{(int64_t)vt->arr_size});
                return make_expr(make_slice_type(vt->elem),
                    lir::ESliceLit{std::move(addr), std::move(len)});
            }
            return make_expr(make_ptr(false, vt), lir::EAddrOf{std::string(var_name)});
        }

        auto operand = lower_expr(map_of(node.get(la::VALUE.code)));
        auto* vt = operand->type;
        if (vt->kind == LogosType::Kind::Error)
            return make_expr(error_t(), lir::EUnary{std::string(op), std::move(operand)});

        // Unary operator overloading for struct/class types
        if (vt->kind == LogosType::Kind::Struct || vt->kind == LogosType::Kind::Class) {
            std::string trait_name, method_name;
            if      (op == "-") { trait_name = "Neg"; method_name = "neg"; }
            else if (op == "!") { trait_name = "Not"; method_name = "not_"; }
            if (!trait_name.empty()) {
                auto type_name = (vt->kind == LogosType::Kind::Struct)
                    ? concrete_struct_name(vt) : concrete_class_name(vt);
                auto mangled = type_name + "__" + method_name;
                auto fit = funcs_.find(mangled);
                if (fit != funcs_.end()) {
                    std::vector<lir::LExprPtr> args;
                    args.push_back(std::move(operand));
                    return make_expr(fit->second.ret_type,
                        lir::ECall{mangled, {}, std::move(args)});
                }
            }
        }

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

        // Check if callee is a closure variable
        auto* callee_type = lookup(callee);
        if (callee_type && callee_type->kind == LogosType::Kind::Closure) {
            std::vector<lir::LExprPtr> arg_exprs;
            if (node.has_key(la::ARGS)) {
                auto args = arr_of(node.get(la::ARGS.code));
                for (uint64_t i = 0; i < args.size(); ++i)
                    arg_exprs.push_back(lower_expr(map_of(args.get(i))));
            }
            auto callee_expr = make_expr(callee_type, lir::EVarRef{std::string(callee)});
            return make_expr(callee_type->closure_ret ? callee_type->closure_ret : void_t(),
                lir::EClosureCall{std::move(callee_expr), std::move(arg_exprs)});
        }

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

        if (fi.is_vararg) {
            // Vararg functions: only check the declared (fixed) args
            if (n_args < fi.param_types.size()) {
                error(std::format("call to vararg '{}': expected at least {} args, got {}",
                      callee, fi.param_types.size(), n_args));
            } else {
                for (uint64_t i = 0; i < fi.param_types.size(); ++i) {
                    auto* at = arg_exprs[i]->type;
                    auto* pt = fi.param_types[i];
                    if (at->kind != LogosType::Kind::Error &&
                        pt->kind != LogosType::Kind::Error &&
                        !types_compatible(at, pt))
                        error(std::format("call to '{}' arg {}: expected {}, got {}",
                              callee, i + 1, type_str(pt), type_str(at)));
                }
            }
        } else if (n_args != fi.param_types.size()) {
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

    lir::LExprPtr lower_class_method_call(lir::LExprPtr recv,
                                           std::string_view cname,
                                           std::string_view method_name,
                                           TinyMapView node) {
        // Walk inheritance chain to find the method.
        std::string resolved_class;
        std::string mangled;
        {
            std::string start_class = std::string(cname);
            std::string cur = start_class;
            // Build subst: start_class type params → receiver's concrete type args
            SemaSubst recv_subst;
            {
                const LogosType* recv_t = recv->type;
                if (recv_t && recv_t->kind == LogosType::Kind::Ptr && recv_t->pointee)
                    recv_t = recv_t->pointee;
                if (recv_t && recv_t->kind == LogosType::Kind::Class &&
                    !recv_t->type_args.empty()) {
                    auto cit = classes_.find(start_class);
                    if (cit != classes_.end()) {
                        auto& tps = cit->second.type_params;
                        for (size_t i = 0; i < tps.size() && i < recv_t->type_args.size(); ++i)
                            recv_subst[tps[i].name] = recv_t->type_args[i];
                    }
                }
            }
            while (!cur.empty()) {
                auto candidate = cur + "__" + std::string(method_name);
                if (funcs_.count(candidate)) {
                    // Only set resolved_class for inherited methods (found on a parent).
                    // When found on the class itself, leave it empty so mlir_gen uses
                    // the concrete type name (e.g., "Box__i32") from gen_recv_struct.
                    if (cur != start_class) {
                        // Compute concrete parent class name using parent_type_args
                        auto sit = classes_.find(start_class);
                        if (!recv_subst.empty() && sit != classes_.end() &&
                            !sit->second.parent_type_args.empty()) {
                            // Substitute parent_type_args to get concrete parent type args
                            std::vector<const LogosType*> concrete_args;
                            for (auto* arg : sit->second.parent_type_args)
                                concrete_args.push_back(subst_type_sema(arg, recv_subst));
                            LogosType parent_t;
                            parent_t.kind = LogosType::Kind::Class;
                            parent_t.struct_name = cur;
                            parent_t.type_args = concrete_args;
                            resolved_class = concrete_class_name(&parent_t);
                        } else {
                            resolved_class = cur;
                        }
                    }
                    mangled = candidate;
                    break;
                }
                auto cit = classes_.find(cur);
                if (cit == classes_.end()) break;
                cur = cit->second.parent_name;
            }
        }
        auto fit = funcs_.find(mangled);
        if (fit == funcs_.end()) {
            error(std::format("class '{}' has no method '{}'", cname, method_name));
            std::vector<lir::LExprPtr> dummy_args;
            return make_expr(error_t(),
                lir::EMethodCall{std::move(recv), std::string(method_name), std::move(dummy_args), -1});
        }

        std::vector<lir::LExprPtr> arg_exprs;
        if (node.has_key(la::ARGS)) {
            auto args = arr_of(node.get(la::ARGS.code));
            for (uint64_t i = 0; i < args.size(); ++i)
                arg_exprs.push_back(lower_expr(map_of(args.get(i))));
        }

        auto& fi = fit->second;

        // Build substitution map for generic class: T → i32, etc.
        SemaSubst class_subst;
        {
            const LogosType* cls_t = recv->type;
            if (cls_t->kind == LogosType::Kind::Ptr && cls_t->pointee)
                cls_t = cls_t->pointee;
            if (cls_t->kind == LogosType::Kind::Class && !cls_t->type_args.empty()) {
                auto cit = classes_.find(std::string(cname));
                if (cit != classes_.end()) {
                    auto& tps = cit->second.type_params;
                    for (size_t i = 0; i < tps.size() && i < cls_t->type_args.size(); ++i)
                        class_subst[tps[i].name] = cls_t->type_args[i];
                }
            }
        }

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
                    auto* pt = class_subst.empty() ? fi.param_types[pi]
                                                   : subst_type_sema(fi.param_types[pi], class_subst);
                    if (at->kind != LogosType::Kind::Error && pt->kind != LogosType::Kind::Error &&
                        !compat(at, pt))
                        error(std::format("method '{}' arg {}: expected {}, got {}",
                              mangled, i + 1, type_str(pt), type_str(at)));
                }
            }
        }

        const LogosType* ret_t = class_subst.empty() ? fi.ret_type
                                                      : subst_type_sema(fi.ret_type, class_subst);

        int32_t vidx = vtable_index_of(cname, mangled);

        lir::EMethodCall mc;
        mc.receiver      = std::move(recv);
        mc.method        = std::string(method_name);
        mc.args          = std::move(arg_exprs);
        mc.vtable_index  = vidx;
        mc.resolved_type = resolved_class;
        return make_expr(ret_t, std::move(mc));
    }

    lir::LExprPtr lower_method_call(TinyMapView node) {
        auto method_name = str_of(node.get(la::NAME.code));
        auto recv = lower_expr(map_of(node.get(la::RECEIVER.code)));

        // Slice built-in methods: .len()
        if (recv->type->kind == LogosType::Kind::Slice) {
            if (method_name == "len") {
                return make_expr(prim(LogosType::Kind::I64),
                    lir::ESliceLen{std::move(recv)});
            }
            error(std::format("slice has no method '{}'", method_name));
            return error_expr();
        }

        // TypeVar with trait bounds: look up trait method signature.
        // The actual impl method will be resolved during monomorphization.
        // Handle both T and *mut T / *const T receivers.
        const LogosType* recv_inner = recv->type;
        if (recv_inner->kind == LogosType::Kind::Ptr && recv_inner->pointee)
            recv_inner = recv_inner->pointee;
        if (recv_inner->kind == LogosType::Kind::TypeVar) {
            // Find trait bounds for this type var
            for (auto& [tvn, tv] : current_type_params_) {
                if (tvn != recv->type->type_var_name) continue;
                // tvn matches — but we need the TypeParam with bounds.
                // Search the current function's type params.
            }
            // Search all type params in scope for this TypeVar's bounds
            const LogosType* ret_type = error_t();
            bool found = false;
            // Walk all currently in-scope functions' type params
            // For now, look through all known traits for the method
            for (auto& [tname, tinfo] : traits_) {
                for (auto& m : tinfo.methods) {
                    if (m.name == method_name) {
                        ret_type = m.ret_type;
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
            if (found) {
                std::vector<lir::LExprPtr> arg_exprs;
                if (node.has_key(la::ARGS)) {
                    auto args = arr_of(node.get(la::ARGS.code));
                    for (uint64_t i = 0; i < args.size(); ++i)
                        arg_exprs.push_back(lower_expr(map_of(args.get(i))));
                }
                // Use EMethodCall — mono will resolve to concrete impl.
                lir::EMethodCall mc;
                mc.receiver = std::move(recv);
                mc.method   = std::string(method_name);
                mc.args     = std::move(arg_exprs);
                mc.vtable_index = -1;
                return make_expr(ret_type, std::move(mc));
            }
        }

        // Check if receiver is a class type → virtual dispatch
        auto cname = class_name_from_type(recv->type);
        if (!cname.empty()) {
            return lower_class_method_call(std::move(recv), cname, method_name, node);
        }

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

        // Substitute struct type params (TypeVars) in return type and param types
        // using the actual type_args of the receiver.  Handles e.g. Pair<T>.swap()
        // where the receiver is Pair<i32> and ret_type is Pair<T> → Pair<i32>.
        const LogosType* recv_struct_t = recv->type;
        if (recv_struct_t->kind == LogosType::Kind::Ptr && recv_struct_t->pointee)
            recv_struct_t = recv_struct_t->pointee;
        const LogosType* ret = fi.ret_type;
        if (recv_struct_t->kind == LogosType::Kind::Struct &&
            !recv_struct_t->type_args.empty()) {
            auto sit = structs_.find(recv_struct_t->struct_name);
            if (sit != structs_.end()) {
                SemaSubst subst;
                auto& tps = sit->second.type_params;
                for (size_t i = 0; i < tps.size() && i < recv_struct_t->type_args.size(); ++i)
                    subst[tps[i].name] = recv_struct_t->type_args[i];
                ret = subst_type_sema(fi.ret_type, subst);
            }
        }

        return make_expr(ret,
            lir::EMethodCall{std::move(recv), std::string(method_name), std::move(arg_exprs)});
    }

    lir::LExprPtr lower_field_read(TinyMapView node) {
        auto field_name = str_of(node.get(la::FIELD.code));
        auto recv = lower_expr(map_of(node.get(la::RECEIVER.code)));

        // Check for class receiver
        auto cname_sv = class_name_from_type(recv->type);
        if (!cname_sv.empty()) {
            auto* ft = class_field_type(cname_sv, field_name);
            if (!ft) {
                error(std::format("field read: class '{}' has no field '{}'", cname_sv, field_name));
                return make_expr(error_t(), lir::EFieldRead{std::move(recv), std::string(field_name)});
            }
            return make_expr(ft, lir::EFieldRead{std::move(recv), std::string(field_name)});
        }

        auto sname = struct_name_from_type(recv->type);
        if (sname.empty()) {
            error(std::format("field read: receiver is not a struct or class (got {})",
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

        lir::LExprPtr idx = node.has_key(la::VALUE)
            ? lower_expr(map_of(node.get(la::VALUE.code)))
            : error_expr();
        if (!is_integer(idx->type))
            error(std::format("array index must be integer, got {}", type_str(idx->type)));

        // Slice indexing: s[i] → ESliceIndex
        if (arr_type->kind == LogosType::Kind::Slice) {
            auto* elem = arr_type->elem ? arr_type->elem : error_t();
            return make_expr(elem, lir::ESliceIndex{std::move(recv), std::move(idx)});
        }

        if (arr_type->kind != LogosType::Kind::Array &&
            arr_type->kind != LogosType::Kind::Ptr &&
            arr_type->kind != LogosType::Kind::Error) {
            error(std::format("index read: receiver is not an array, slice, or pointer (got {})",
                  type_str(arr_type)));
        }

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

    lir::LExprPtr lower_enum_lit_data(TinyMapView node) {
        auto ename = str_of(node.get(la::NAME.code));
        auto vname = str_of(node.get(la::FIELD.code));
        auto eit = enums_.find(std::string(ename));
        if (eit == enums_.end()) {
            error(std::format("unknown enum '{}'", ename));
            return error_expr();
        }
        const SemaVariantInfo* vinfo = nullptr;
        for (auto& v : eit->second.variants)
            if (v.name == vname) { vinfo = &v; break; }
        if (!vinfo) {
            error(std::format("enum '{}' has no variant '{}'", ename, vname));
            return error_expr();
        }

        // Lower payload arguments
        std::vector<lir::LExprPtr> payload;
        if (node.has_key(la::ARGS)) {
            auto args = arr_of(node.get(la::ARGS.code));
            for (uint64_t i = 0; i < args.size(); ++i)
                payload.push_back(lower_expr(map_of(args.get(i))));
        }

        // Resolve payload types — substitute TypeVars if generic enum
        auto& einfo = eit->second;
        std::vector<const LogosType*> resolved_payload_types = vinfo->payload_types;

        // Build the enum type (may be generic, e.g. Option<i32>)
        // For now, if the enum has type params, we need to infer them from payload types.
        // Simple inference: match payload args to payload type params.
        const LogosType* result_type = make_enum_type(ename);
        if (!einfo.type_params.empty() && !payload.empty()) {
            // Build substitution from payload args
            SemaSubst subst;
            for (size_t i = 0; i < vinfo->payload_types.size() && i < payload.size(); ++i) {
                auto* pt = vinfo->payload_types[i];
                if (pt && pt->kind == LogosType::Kind::TypeVar) {
                    auto* inferred = payload[i]->type;
                    if (inferred->kind == LogosType::Kind::IntLit) inferred = i32_t();
                    subst[pt->type_var_name] = inferred;
                }
            }
            // Build concrete type args
            std::vector<const LogosType*> type_args;
            for (auto& tp : einfo.type_params) {
                auto sit = subst.find(tp.name);
                type_args.push_back(sit != subst.end() ? sit->second : error_t());
            }
            LogosType et; et.kind = LogosType::Kind::Enum;
            et.enum_name = std::string(ename);
            et.type_args = std::move(type_args);
            result_type = pool_.alloc(std::move(et));
            // Resolve payload types with substitution
            for (size_t i = 0; i < resolved_payload_types.size(); ++i)
                resolved_payload_types[i] = subst_type_sema(resolved_payload_types[i], subst);
        }

        // Type-check payload args against expected types
        if (payload.size() != vinfo->payload_types.size()) {
            error(std::format("{}::{} expects {} args, got {}",
                  ename, vname, vinfo->payload_types.size(), payload.size()));
        } else {
            for (size_t i = 0; i < payload.size(); ++i) {
                if (payload[i]->type->kind != LogosType::Kind::Error &&
                    resolved_payload_types[i] &&
                    resolved_payload_types[i]->kind != LogosType::Kind::Error &&
                    !types_compatible(payload[i]->type, resolved_payload_types[i]))
                    error(std::format("{}::{} arg {}: expected {}, got {}",
                          ename, vname, i, type_str(resolved_payload_types[i]),
                          type_str(payload[i]->type)));
            }
        }

        return make_expr(result_type,
            lir::EEnumLitData{std::string(ename), std::string(vname),
                              vinfo->value, std::move(payload)});
    }

    // Helper: handle STATIC_CALL that's actually an enum variant with data.
    lir::LExprPtr lower_enum_lit_data_from_static(
            TinyMapView node, std::string_view ename, std::string_view vname) {
        auto eit = enums_.find(std::string(ename));
        if (eit == enums_.end()) return error_expr();
        const SemaVariantInfo* vinfo = nullptr;
        for (auto& v : eit->second.variants)
            if (v.name == vname) { vinfo = &v; break; }
        if (!vinfo) {
            error(std::format("enum '{}' has no variant '{}'", ename, vname));
            return error_expr();
        }
        // Lower args
        std::vector<lir::LExprPtr> payload;
        if (node.has_key(la::ARGS)) {
            AnyVal args_av = node.get(la::ARGS.code);
            if (!args_av.is_null()) {
                auto args = map_of(args_av);
                if (args.has_key(la::ITEMS)) {
                    auto items = arr_of(args.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < items.size(); ++i)
                        payload.push_back(lower_expr(map_of(items.get(i))));
                }
            }
        }
        // Build result type + type-check (same logic as lower_enum_lit_data)
        auto& einfo = eit->second;
        std::vector<const LogosType*> resolved_payload_types = vinfo->payload_types;
        const LogosType* result_type = make_enum_type(ename);
        if (!einfo.type_params.empty() && !payload.empty()) {
            SemaSubst subst;
            for (size_t i = 0; i < vinfo->payload_types.size() && i < payload.size(); ++i) {
                auto* pt = vinfo->payload_types[i];
                if (pt && pt->kind == LogosType::Kind::TypeVar) {
                    auto* inferred = payload[i]->type;
                    if (inferred->kind == LogosType::Kind::IntLit) inferred = i32_t();
                    subst[pt->type_var_name] = inferred;
                }
            }
            std::vector<const LogosType*> type_args;
            for (auto& tp : einfo.type_params) {
                auto sit = subst.find(tp.name);
                type_args.push_back(sit != subst.end() ? sit->second : error_t());
            }
            LogosType et; et.kind = LogosType::Kind::Enum;
            et.enum_name = std::string(ename);
            et.type_args = std::move(type_args);
            result_type = pool_.alloc(std::move(et));
            for (size_t i = 0; i < resolved_payload_types.size(); ++i)
                resolved_payload_types[i] = subst_type_sema(resolved_payload_types[i], subst);
        }
        if (payload.size() != vinfo->payload_types.size()) {
            error(std::format("{}::{} expects {} args, got {}",
                  ename, vname, vinfo->payload_types.size(), payload.size()));
        }
        return make_expr(result_type,
            lir::EEnumLitData{std::string(ename), std::string(vname),
                              vinfo->value, std::move(payload)});
    }

    lir::LExprPtr lower_new_expr(TinyMapView node) {
        auto cname = str_of(node.get(la::NAME.code));
        auto cit = classes_.find(std::string(cname));
        if (cit == classes_.end()) {
            error(std::format("'new': unknown class '{}'", cname));
            return error_expr();
        }
        auto& cinfo = cit->second;
        if (cinfo.is_abstract) {
            error(std::format("'new': cannot instantiate abstract class '{}'", cname));
            return error_expr();
        }

        std::vector<std::pair<std::string, lir::LExprPtr>> fields;
        if (node.has_key(la::ITEMS)) {
            auto inits = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < inits.size(); ++i) {
                auto init = map_of(inits.get(i));
                if (code_of(init) != la::FIELD_INIT) continue;  // skip type_arg_list or other non-field items
                auto fname = str_of(init.get(la::NAME.code));
                lir::LExprPtr val = init.has_key(la::VALUE)
                    ? lower_expr(map_of(init.get(la::VALUE.code)))
                    : error_expr();
                fields.push_back({std::string(fname), std::move(val)});
            }
        }

        // Validate all fields are initialized and no extras
        std::unordered_map<std::string, bool> initialized;
        for (auto& [fn, ft] : cinfo.all_fields) initialized[fn] = false;
        for (auto& [fname, fval] : fields) {
            auto it = initialized.find(fname);
            if (it == initialized.end()) {
                error(std::format("'new {}': unknown field '{}'", cname, fname));
            } else {
                it->second = true;
                // Find expected type; skip check if field type is TypeVar (checked post-mono)
                for (auto& [fn, ft] : cinfo.all_fields) {
                    if (fn == fname && ft->kind != LogosType::Kind::Error &&
                        ft->kind != LogosType::Kind::TypeVar &&
                        fval->type->kind != LogosType::Kind::Error &&
                        !compat(fval->type, ft)) {
                        error(std::format("'new {}' field '{}': expected {}, got {}",
                              cname, fname, type_str(ft), type_str(fval->type)));
                    }
                }
            }
        }
        for (auto& [fn, init] : initialized)
            if (!init)
                error(std::format("'new {}': field '{}' not initialized", cname, fn));

        // For generic classes, use explicit type args if provided, else infer from fields.
        const LogosType* class_t = nullptr;
        if (!cinfo.type_params.empty()) {
            std::vector<const LogosType*> args;
            if (node.has_key(la::TYPE_PARAMS)) {
                // Explicit: new Box<i32> { ... }
                // TYPE_PARAMS is a type_arg_list node: { ITEMS: [type_ref, ...] }
                auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
                auto type_items = tplist.has_key(la::ITEMS)
                                    ? arr_of(tplist.get(la::ITEMS.code))
                                    : arr_of(node.get(la::TYPE_PARAMS.code));
                for (uint64_t i = 0; i < type_items.size(); ++i)
                    args.push_back(resolve_type(map_of(type_items.get(i))));
            } else {
                // Infer from field values
                SemaSubst inferred;
                for (auto& [fname, fval] : fields) {
                    for (auto& [fn, ft] : cinfo.all_fields) {
                        if (fn != fname) continue;
                        if (ft->kind == LogosType::Kind::TypeVar) {
                            auto& tv = ft->type_var_name;
                            if (!inferred.count(tv)) {
                                auto* vt = fval->type;
                                if (vt->kind == LogosType::Kind::IntLit) vt = i32_t();
                                inferred[tv] = vt;
                            }
                        }
                    }
                }
                for (auto& tp : cinfo.type_params) {
                    auto it = inferred.find(tp.name);
                    args.push_back(it != inferred.end() ? it->second : error_t());
                }
            }
            class_t = make_generic_class(cname, std::move(args));
        } else {
            class_t = make_class_type(cname);
        }
        auto* result_t = make_ptr(true, class_t);
        return make_expr(result_t, lir::ENew{std::string(cname), std::move(fields)});
    }

    // ── lower_static_call: ClassName::method(args) ───────────────
    lir::LExprPtr lower_static_call(TinyMapView node) {
        auto class_name = str_of(node.get(la::RECEIVER.code));
        auto method_name = str_of(node.get(la::NAME.code));

        // If "class_name" is actually an enum, redirect to enum lit with data.
        if (enums_.count(std::string(class_name))) {
            // Reinterpret as ENUM_LIT_DATA: NAME=class_name, FIELD=method_name
            // Build a fake node view... or just inline the logic.
            return lower_enum_lit_data_from_static(node, class_name, method_name);
        }

        std::string mangled = std::string(class_name) + "__" + std::string(method_name);

        auto fit = funcs_.find(mangled);

        std::vector<lir::LExprPtr> arg_exprs;
        if (node.has_key(la::ARGS)) {
            AnyVal args_av = node.get(la::ARGS.code);
            if (!args_av.is_null()) {
                auto args = map_of(args_av);
                if (args.has_key(la::ITEMS)) {
                    auto items = arr_of(args.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < items.size(); ++i)
                        arg_exprs.push_back(lower_expr(map_of(items.get(i))));
                }
            }
        }

        if (fit == funcs_.end()) {
            error(std::format("call to undefined static method '{}::{}'", class_name, method_name));
            return make_expr(error_t(), lir::ECall{mangled, {}, std::move(arg_exprs)});
        }

        auto& fi = fit->second;
        uint64_t n_args = arg_exprs.size();
        if (n_args != fi.param_types.size()) {
            error(std::format("static call '{}': expected {} args, got {}",
                  mangled, fi.param_types.size(), n_args));
        } else {
            for (uint64_t i = 0; i < n_args; ++i) {
                auto* at = arg_exprs[i]->type;
                auto* pt = fi.param_types[i];
                if (at->kind != LogosType::Kind::Error &&
                    pt->kind != LogosType::Kind::Error &&
                    !types_compatible(at, pt))
                    error(std::format("static call '{}' arg {}: expected {}, got {}",
                          mangled, i + 1, type_str(pt), type_str(at)));
            }
        }

        return make_expr(fi.ret_type, lir::ECall{mangled, {}, std::move(arg_exprs)});
    }

    // ── lower_if_expr: if cond { expr } else { expr } ────────────
    lir::LExprPtr lower_if_expr(TinyMapView node) {
        lir::LExprPtr cond;
        if (node.has_key(la::COND)) {
            cond = lower_expr(map_of(node.get(la::COND.code)));
            if (cond->type->kind != LogosType::Kind::Bool &&
                cond->type->kind != LogosType::Kind::Error)
                error(std::format("if condition must be bool, got {}", type_str(cond->type)));
        } else {
            cond = error_expr();
        }

        // Both branches must be single-expression blocks (last expr is the value)
        // For simplicity: require both THEN and ELSE branches (else is required for expr form)
        if (!node.has_key(la::ELSE)) {
            error("if-as-expression requires an else branch");
            return error_expr();
        }

        // Lower the last expression from each block
        lir::LExprPtr then_val = error_expr();
        lir::LExprPtr else_val = error_expr();

        auto lower_block_last_expr = [&](TinyMapView blk) -> lir::LExprPtr {
            if (blk.is_null() || !blk.has_key(la::ITEMS)) return error_expr();
            auto stmts = arr_of(blk.get(la::ITEMS.code));
            if (stmts.size() == 0) { error("if-as-expression: empty branch"); return error_expr(); }
            // Last statement must be a bare expression (no semicolon → no EXPR_STMT wrapper in grammar)
            // In our grammar, the items in a block are stmts; bare expr has CODE: EXPR_STMT with VALUE.
            // But if used as expression, the last item should be a raw expr node (not EXPR_STMT).
            // Actually in our grammar, `if expr block` — blocks contain stmts, and stmts always end
            // with semicolons. The grammar doesn't support bare expression at end of block.
            // So lower_if_expr treats the block's stmts as statements and the last
            // EXPR_STMT's VALUE as the branch value.
            auto last = map_of(stmts.get(stmts.size() - 1));
            int32_t lc = code_of(last);
            if (lc == la::EXPR_STMT && last.has_key(la::VALUE)) {
                // Lower all preceding stmts as side effects (ignored in expr mode)
                return lower_expr(map_of(last.get(la::VALUE.code)));
            }
            // Otherwise, try treating it as a direct expression (for inline if_expr in primary_expr)
            return lower_expr(last);
        };

        if (node.has_key(la::THEN)) {
            auto then_node = map_of(node.get(la::THEN.code));
            if (code_of(then_node) == la::BLOCK)
                then_val = lower_block_last_expr(then_node);
            else
                then_val = lower_expr(then_node);
        }

        auto else_node = map_of(node.get(la::ELSE.code));
        if (code_of(else_node) == la::BLOCK)
            else_val = lower_block_last_expr(else_node);
        else
            else_val = lower_expr(else_node);

        // Determine result type
        const LogosType* result_type = then_val->type;
        if (then_val->type->kind == LogosType::Kind::Error)
            result_type = else_val->type;
        else if (else_val->type->kind != LogosType::Kind::Error &&
                 !types_compatible(then_val->type, else_val->type) &&
                 !types_compatible(else_val->type, then_val->type)) {
            error(std::format("if-expression branches have incompatible types: {} vs {}",
                  type_str(then_val->type), type_str(else_val->type)));
        }

        lir::EIfExpr eif;
        eif.cond      = std::move(cond);
        eif.then_val  = std::move(then_val);
        eif.else_val  = std::move(else_val);
        return make_expr(result_type, std::move(eif));
    }

    // ── lower_closure_expr ────────────────────────────────────────
    lir::LExprPtr lower_closure_expr(TinyMapView node) {
        auto closure_id = "__closure_" + std::to_string(closure_counter_++);

        // Parse parameters
        std::vector<lir::LParam> params;
        std::vector<const LogosType*> param_types;
        if (node.has_key(la::PARAMS)) {
            AnyVal pav = node.get(la::PARAMS.code);
            if (!pav.is_null() && pav.is_pointer()) {
                auto plist = map_of(pav);
                if (plist.has_key(la::ITEMS)) {
                    auto pitems = arr_of(plist.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < pitems.size(); ++i) {
                        auto p = map_of(pitems.get(i));
                        auto pname = std::string(str_of(p.get(la::NAME.code)));
                        const LogosType* ptype = p.has_key(la::TYPE)
                            ? resolve_type(map_of(p.get(la::TYPE.code))) : error_t();
                        params.push_back({pname, ptype});
                        param_types.push_back(ptype);
                    }
                }
            }
        }

        const LogosType* ret_type = node.has_key(la::RET_TYPE)
            ? resolve_type(map_of(node.get(la::RET_TYPE.code))) : void_t();

        // Push a new scope with closure params
        push_scope();
        for (auto& p : params)
            define(p.name, p.type);

        // Collect current scope variables (for capture detection)
        std::unordered_set<std::string> param_names;
        for (auto& p : params) param_names.insert(p.name);

        // Lower body
        auto saved_ret = ret_type_;
        ret_type_ = ret_type;
        lir::LBlock body;
        if (node.has_key(la::BODY)) {
            auto body_node = map_of(node.get(la::BODY.code));
            if (code_of(body_node) == la::BLOCK)
                body = lower_block(body_node);
        }
        ret_type_ = saved_ret;
        pop_scope();

        // Capture detection: find variables used in body that are not params
        // and exist in the enclosing scope.
        std::vector<std::string> captures;
        std::vector<const LogosType*> capture_types;
        std::unordered_set<std::string> seen;
        std::function<void(const lir::LExpr&)> scan_captures;
        scan_captures = [&](const lir::LExpr& e) {
            if (auto* vr = std::get_if<lir::EVarRef>(&e.kind)) {
                if (!param_names.count(vr->name) && !seen.count(vr->name)) {
                    auto* t = lookup(vr->name);
                    if (t) {
                        captures.push_back(vr->name);
                        capture_types.push_back(t);
                        seen.insert(vr->name);
                    }
                }
            }
            // Recurse into sub-expressions
            std::visit([&](const auto& k) {
                using K = std::decay_t<decltype(k)>;
                if constexpr (std::is_same_v<K, lir::EBinOp>) {
                    scan_captures(*k.lhs); scan_captures(*k.rhs);
                } else if constexpr (std::is_same_v<K, lir::EUnary>) {
                    scan_captures(*k.operand);
                } else if constexpr (std::is_same_v<K, lir::ECall>) {
                    for (auto& a : k.args) scan_captures(*a);
                } else if constexpr (std::is_same_v<K, lir::EMethodCall>) {
                    scan_captures(*k.receiver);
                    for (auto& a : k.args) scan_captures(*a);
                } else if constexpr (std::is_same_v<K, lir::EFieldRead>) {
                    scan_captures(*k.receiver);
                } else if constexpr (std::is_same_v<K, lir::EIndexRead>) {
                    scan_captures(*k.receiver); scan_captures(*k.index);
                } else if constexpr (std::is_same_v<K, lir::EDeref>) {
                    scan_captures(*k.operand);
                } else if constexpr (std::is_same_v<K, lir::ECast>) {
                    scan_captures(*k.operand);
                } else if constexpr (std::is_same_v<K, lir::EIfExpr>) {
                    scan_captures(*k.cond); scan_captures(*k.then_val); scan_captures(*k.else_val);
                }
            }, e.kind);
        };
        // Scan all statements in body for variable references
        std::function<void(const lir::LBlock&)> scan_block;
        std::function<void(const lir::LStmt&)> scan_stmt;
        scan_block = [&](const lir::LBlock& b) {
            for (auto& s : b.stmts) scan_stmt(s);
        };
        scan_stmt = [&](const lir::LStmt& s) {
            std::visit([&](const auto& k) {
                using K = std::decay_t<decltype(k)>;
                if constexpr (std::is_same_v<K, lir::SLet>) {
                    scan_captures(*k.value);
                } else if constexpr (std::is_same_v<K, lir::SAssign>) {
                    scan_captures(*k.value);
                } else if constexpr (std::is_same_v<K, lir::SReturn>) {
                    if (k.value) scan_captures(*k.value);
                } else if constexpr (std::is_same_v<K, lir::SIf>) {
                    scan_captures(*k.cond); scan_block(*k.then_);
                    if (k.else_) scan_block(**k.else_);
                } else if constexpr (std::is_same_v<K, lir::SWhile>) {
                    scan_captures(*k.cond); scan_block(*k.body);
                } else if constexpr (std::is_same_v<K, lir::SExprStmt>) {
                    scan_captures(*k.expr);
                }
            }, s.kind);
        };
        scan_block(body);

        auto ec = std::make_unique<lir::EClosure>();
        ec->closure_id    = closure_id;
        ec->params        = std::move(params);
        ec->ret_type      = ret_type;
        ec->body          = std::move(body);
        ec->captures      = std::move(captures);
        ec->capture_types = std::move(capture_types);

        auto* ctype = make_closure_type(std::move(param_types), ret_type);
        return make_expr(ctype, lir::EClosureBox{std::move(ec)});
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
        if (c == la::FOR_EACH)     return lower_for_each(stmt);
        if (c == la::LOOP)         return lower_loop(stmt);
        if (c == la::FIELD_WRITE)        return lower_field_write(stmt);
        if (c == la::INDEX_WRITE)        return lower_index_write(stmt);
        if (c == la::FIELD_INDEX_WRITE)  return lower_field_index_write(stmt);
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
        if (c == la::DELETE_STMT) {
            lir::LExprPtr expr = stmt.has_key(la::VALUE)
                ? lower_expr(map_of(stmt.get(la::VALUE.code)))
                : error_expr();
            auto* et = expr->type;
            if (et->kind != LogosType::Kind::Ptr || !et->pointee ||
                (et->pointee->kind != LogosType::Kind::Class &&
                 et->pointee->kind != LogosType::Kind::Error)) {
                error(std::format("'delete' requires a class pointer, got {}",
                      type_str(et)));
            }
            return make_stmt(node_line_, lir::SDelete{std::move(expr)});
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

    // ── for item in array — lowered to SForEach ─────────────────────
    lir::LStmt lower_for_each(TinyMapView node) {
        auto var_name = str_of(node.get(la::NAME.code));

        lir::LExprPtr iter = node.has_key(la::ITER)
            ? lower_expr(map_of(node.get(la::ITER.code))) : error_expr();

        // Determine array size from type
        const LogosType* iter_type = iter->type;
        int64_t arr_size = 0;
        const LogosType* elem_type = i32_t();

        if (iter_type->kind == LogosType::Kind::Array) {
            arr_size = (int64_t)iter_type->arr_size;
            elem_type = iter_type->elem ? iter_type->elem : i32_t();
        } else if (iter_type->kind != LogosType::Kind::Error) {
            error(std::format("for-each: '{}' is not an array type, got {}",
                  var_name, type_str(iter_type)));
        }

        push_scope();
        define(var_name, elem_type, false);

        auto body = std::make_unique<lir::LBlock>();
        if (node.has_key(la::BODY)) {
            ++loop_depth_;
            *body = lower_block(map_of(node.get(la::BODY.code)));
            --loop_depth_;
        }

        pop_scope();

        lir::SForEach sfe;
        sfe.var       = std::string(var_name);
        sfe.iter      = std::move(iter);
        sfe.elem_type = elem_type;
        sfe.arr_size  = arr_size;
        sfe.body      = std::move(body);
        return make_stmt(node_line_, std::move(sfe));
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
        auto cname = sname.empty() ? class_name_of(recv_name) : std::string_view{};
        bool is_class = !cname.empty();
        if (sname.empty() && cname.empty()) {
            error(std::format("field write: '{}' is not a struct or class", recv_name));
        } else {
            auto* recv_type = lookup(recv_name);
            if (recv_type && recv_type->kind == LogosType::Kind::Ptr) {
                if (!recv_type->mut_ptr)
                    error(std::format("field write to '{}': receiver is *const pointer", recv_name));
            } else if (!is_class && !lookup_is_mut(recv_name)) {
                error(std::format("field write to immutable variable '{}'", recv_name));
            }
        }
        const LogosType* recv_struct_t = (sname.empty() && cname.empty()) ? nullptr : lookup(recv_name);
        if (recv_struct_t && recv_struct_t->kind == LogosType::Kind::Ptr)
            recv_struct_t = recv_struct_t->pointee;
        const LogosType* ft = nullptr;
        if (recv_struct_t) {
            if (is_class) {
                // For classes, apply TypeVar substitution to field types.
                SemaSubst subst;
                auto cit = classes_.find(std::string(cname));
                if (cit != classes_.end() && recv_struct_t->type_args.size() == cit->second.type_params.size()) {
                    for (size_t i = 0; i < cit->second.type_params.size(); ++i)
                        subst[cit->second.type_params[i].name] = recv_struct_t->type_args[i];
                }
                auto* raw_ft = class_field_type(cname, field_name);
                ft = (raw_ft && !subst.empty()) ? subst_type_sema(raw_ft, subst) : raw_ft;
            } else {
                ft = field_type_of_for_type(recv_struct_t, field_name);
            }
        }
        if (!sname.empty() && !ft)
            error(std::format("field write: struct '{}' has no field '{}'", sname, field_name));
        if (!cname.empty() && !ft)
            error(std::format("field write: class '{}' has no field '{}'", cname, field_name));

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

    // a.field[index] = value;  (e.g. self.ptr[i] = val)
    lir::LStmt lower_field_index_write(TinyMapView node) {
        auto recv_name  = str_of(node.get(la::RECEIVER.code));
        auto field_name = str_of(node.get(la::FIELD.code));

        // Resolve field type — must be *mut T.
        auto* recv_t = lookup(recv_name);
        if (!recv_t) error(std::format("field index write: undefined variable '{}'", recv_name));

        // Unwrap pointer receiver (class/struct-via-ptr).
        const LogosType* base_t = recv_t;
        if (base_t && base_t->kind == LogosType::Kind::Ptr) base_t = base_t->pointee;

        const LogosType* field_t = nullptr;
        if (base_t) {
            auto sname = struct_name_from_type(base_t);
            auto cname = class_name_from_type(base_t);
            if (!sname.empty())       field_t = field_type_of_for_type(base_t, field_name);
            else if (!cname.empty())  field_t = class_field_type(cname, field_name);
        }

        if (!field_t)
            error(std::format("field index write: cannot resolve field '{}.{}'", recv_name, field_name));

        if (field_t && field_t->kind != LogosType::Kind::Ptr)
            error(std::format("field index write: field '{}.{}' is not a pointer (got {})",
                  recv_name, field_name, type_str(field_t)));
        if (field_t && field_t->kind == LogosType::Kind::Ptr && !field_t->mut_ptr)
            error(std::format("field index write: field '{}.{}' is *const, cannot write",
                  recv_name, field_name));

        const LogosType* elem_t = (field_t && field_t->kind == LogosType::Kind::Ptr)
                                   ? field_t->pointee : nullptr;

        lir::LExprPtr idx = node.has_key(la::LHS)
            ? lower_expr(map_of(node.get(la::LHS.code))) : error_expr();
        if (!is_integer(idx->type))
            error(std::format("field index write: index must be integer, got {}", type_str(idx->type)));

        lir::LExprPtr val = node.has_key(la::VALUE)
            ? lower_expr(map_of(node.get(la::VALUE.code))) : error_expr();
        if (elem_t && elem_t->kind != LogosType::Kind::Error &&
            val->type->kind != LogosType::Kind::Error &&
            !types_compatible(val->type, elem_t)) {
            error(std::format("field index write '{}.{}[i]': expected {}, got {}",
                  recv_name, field_name, type_str(elem_t), type_str(val->type)));
        }

        lir::SFieldIndexWrite sfiw;
        sfiw.receiver = std::string(recv_name);
        sfiw.field    = std::string(field_name);
        sfiw.index    = std::move(idx);
        sfiw.value    = std::move(val);
        return make_stmt(node_line_, std::move(sfiw));
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
                    } else if (pc == la::PAT_VARIANT_DATA) {
                        auto pename = std::string(str_of(pnode.get(la::NAME.code)));
                        auto pvname = std::string(str_of(pnode.get(la::FIELD.code)));
                        int32_t disc = 0;
                        const SemaVariantInfo* vinfo = nullptr;
                        auto eit = enums_.find(pename);
                        if (eit == enums_.end()) {
                            error(std::format("match pattern: unknown enum '{}'", pename));
                        } else {
                            for (auto& v : eit->second.variants)
                                if (v.name == pvname) { vinfo = &v; disc = v.value; break; }
                            if (!vinfo)
                                error(std::format("match: enum '{}' has no variant '{}'",
                                      pename, pvname));
                        }
                        // Read binding names from ARGS (pat_binding_list)
                        std::vector<std::string> bindings;
                        if (pnode.has_key(la::ARGS)) {
                            AnyVal aav = pnode.get(la::ARGS.code);
                            if (!aav.is_null() && aav.is_pointer()) {
                                auto blist = map_of(aav);
                                if (blist.has_key(la::ITEMS)) {
                                    auto bitems = arr_of(blist.get(la::ITEMS.code));
                                    for (uint64_t j = 0; j < bitems.size(); ++j) {
                                        auto bnode = map_of(bitems.get(j));
                                        bindings.push_back(std::string(str_of(bnode.get(la::NAME.code))));
                                    }
                                }
                            }
                        }
                        // Resolve binding types from variant payload
                        std::vector<const LogosType*> binding_types;
                        if (vinfo) {
                            // Substitute TypeVars using scrut's type_args
                            SemaSubst subst;
                            if (scrut_type->kind == LogosType::Kind::Enum &&
                                !scrut_type->type_args.empty()) {
                                auto& einfo = eit->second;
                                for (size_t k = 0; k < einfo.type_params.size() &&
                                                    k < scrut_type->type_args.size(); ++k)
                                    subst[einfo.type_params[k].name] = scrut_type->type_args[k];
                            }
                            for (auto* pt : vinfo->payload_types)
                                binding_types.push_back(subst.empty() ? pt : subst_type_sema(pt, subst));
                        }
                        if (bindings.size() != binding_types.size()) {
                            error(std::format("match {}::{}: expected {} bindings, got {}",
                                  pename, pvname,
                                  binding_types.size(), bindings.size()));
                        }
                        pat = lir::PatVariantData{pename, pvname, disc,
                                                   std::move(bindings), std::move(binding_types)};
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

                // Build body block — push pattern bindings into scope
                push_scope();
                if (auto* pvd = std::get_if<lir::PatVariantData>(&pat)) {
                    for (size_t bi = 0; bi < pvd->bindings.size() &&
                                        bi < pvd->binding_types.size(); ++bi)
                        define(pvd->bindings[bi], pvd->binding_types[bi]);
                }
                lir::LBlockPtr body = std::make_unique<lir::LBlock>();
                if (arm.has_key(la::BODY)) {
                    auto body_node = map_of(arm.get(la::BODY.code));
                    if (code_of(body_node) == la::BLOCK) {
                        *body = lower_block(body_node);
                    } else {
                        body->stmts.push_back(lower_stmt(body_node));
                    }
                }
                pop_scope();
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
        int32_t node_code = code_of(node);
        fn.is_extern = (node_code == la::EXTERN_FN);

        // Check is_vararg for extern fn with variadic params
        if (fn.is_extern && node.has_key(la::IS_VARARG)) {
            AnyVal av = node.get(la::IS_VARARG.code);
            fn.is_vararg = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
        }

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
                int32_t mc = code_of(method);
                if (mc == la::FN || mc == la::STATIC_FN)
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
        ed.type_params = einfo.type_params;
        for (auto& v : einfo.variants)
            ed.variants.push_back({std::string(v.name), v.value, v.payload_types});
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

    lir::LTraitDef lower_trait_def(TinyMapView node) {
        auto tname = std::string(str_of(node.get(la::NAME.code)));
        lir::LTraitDef td;
        td.name = tname;
        auto tit = traits_.find(tname);
        if (tit != traits_.end()) {
            for (auto& m : tit->second.methods) {
                lir::LTraitMethodSig sig;
                sig.name     = m.name;
                sig.ret_type = m.ret_type;
                // We don't lower params here since they may contain Self
                td.methods.push_back(std::move(sig));
            }
        }
        return td;
    }

    void lower_impl_block(TinyMapView node, lir::LProgram& prog) {
        std::string trait_name;
        if (node.has_key(la::NAME))
            trait_name = std::string(str_of(node.get(la::NAME.code)));
        std::string target;
        if (node.has_key(la::TYPE)) {
            auto tnode = map_of(node.get(la::TYPE.code));
            target = std::string(str_of(tnode.get(la::NAME.code)));
        }
        lir::LImplBlock ib;
        ib.trait_name   = trait_name;
        ib.target_type  = target;
        // Lower impl methods as free functions (Target__method).
        // Skip for class trait impls (methods already defined in class body).
        bool skip_lower = !trait_name.empty() && classes_.count(target) > 0;
        if (!skip_lower && node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto m = map_of(items.get(i));
                if (code_of(m) == la::FN || code_of(m) == la::STATIC_FN) {
                    auto fn = lower_fn(m, target);
                    prog.functions.push_back(std::move(fn));
                }
            }
        }
        prog.impls.push_back(std::move(ib));
    }

    // ── lower_class_def ──────────────────────────────────────────

    lir::LClassDef lower_class_def(TinyMapView node) {
        auto cname = std::string(str_of(node.get(la::NAME.code)));
        ctx_ = std::format("class {}", cname);

        lir::LClassDef cd;
        cd.name = cname;

        auto& cinfo = classes_[cname];
        cd.is_abstract       = cinfo.is_abstract;
        cd.parent_name       = cinfo.parent_name;
        cd.parent_type_args  = cinfo.parent_type_args;
        cd.vtable_order      = cinfo.vtable_order;
        cd.type_params       = cinfo.type_params;

        push_type_params(cd.type_params);

        // Own fields (not all_fields — parent fields are in parent's LClassDef)
        size_t parent_field_count = 0;
        if (!cinfo.parent_name.empty()) {
            auto pit = classes_.find(cinfo.parent_name);
            if (pit != classes_.end())
                parent_field_count = pit->second.all_fields.size();
        }
        for (size_t i = parent_field_count; i < cinfo.all_fields.size(); ++i) {
            auto& [fname, ftype] = cinfo.all_fields[i];
            cd.own_fields.push_back({fname, ftype});
        }

        // Lower concrete methods and collect static methods separately
        if (node.has_key(la::ITEMS)) {
            auto members = arr_of(node.get(la::ITEMS.code));
            for (uint64_t m = 0; m < members.size(); ++m) {
                auto member = map_of(members.get(m));
                int32_t mc = code_of(member);
                if (mc == la::FN)
                    cd.methods.push_back(lower_fn(member, cname));
                else if (mc == la::STATIC_FN)
                    cd.static_methods.push_back(lower_fn(member, cname));
                // ABSTRACT_FN: no body to lower
            }
        }

        pop_type_params(cd.type_params);
        return cd;
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
            else if (c == la::CLASS) {
                auto cd = lower_class_def(item);
                // Static methods are free functions — emit them to prog.functions
                for (auto& sm : cd.static_methods)
                    prog.functions.push_back(std::move(sm));
                cd.static_methods.clear();
                prog.classes.push_back(std::move(cd));
            }
            else if (c == la::FN || c == la::EXTERN_FN) {
                if (is_specialization_fn(item))
                    prog.specializations.push_back(lower_spec_fn(item));
                else
                    prog.functions.push_back(lower_fn(item));
            }
            else if (c == la::CONST_DEF)  prog.consts.push_back(lower_const_def(item));
            else if (c == la::TYPE_ALIAS) prog.type_aliases.push_back(lower_type_alias_def(item));
            else if (c == la::TRAIT_DEF)  prog.traits.push_back(lower_trait_def(item));
            else if (c == la::IMPL_BLOCK) lower_impl_block(item, prog);
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
