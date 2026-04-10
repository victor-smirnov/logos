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

#include <cstdint>
#include <cstdio>
#include <format>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <optional>

namespace logos::compiler {

// ── types_equal / type_str ─────────────────────────────────────────────────

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

// ── Generic struct name helpers ────────────────────────────────────────────

static std::string mangle_type_for_name(const LogosType* t);

std::string concrete_struct_name(const LogosType* t) {
    if (!t || t->kind != LogosType::Kind::Struct) return {};
    if (t->type_args.empty()) return t->struct_name;
    std::string r = t->struct_name + "$G" + std::to_string(t->type_args.size());
    for (auto* a : t->type_args) { r += "$"; r += mangle_type_for_name(a); }
    return r;
}

std::string concrete_class_name(const LogosType* t) {
    if (!t || t->kind != LogosType::Kind::Class) return {};
    if (t->type_args.empty()) return t->struct_name;
    std::string r = t->struct_name + "$C" + std::to_string(t->type_args.size());
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
        return concrete_struct_name(t);
    case LogosType::Kind::Class:
        return concrete_class_name(t);
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
    // Array with IntLit elements is compatible with Array of any integer element type.
    if (from->kind == LogosType::Kind::Array && to->kind == LogosType::Kind::Array &&
        from->arr_size == to->arr_size && from->elem && to->elem &&
        from->elem->kind == LogosType::Kind::IntLit && is_integer_kind(to->elem->kind))
        return true;
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
    // Struct/Class → &dyn Trait coercion (impl check deferred to codegen)
    if (to->kind == LogosType::Kind::TraitObject &&
        (from->kind == LogosType::Kind::Struct || from->kind == LogosType::Kind::Class ||
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
    case LogosType::Kind::Enum:        return t->enum_name;
    case LogosType::Kind::TraitObject: return "&dyn " + t->trait_name;
    case LogosType::Kind::TypeVar:     return std::string(t->type_var_name);
    case LogosType::Kind::ConstVar:    return std::string(t->type_var_name);
    case LogosType::Kind::AssocType:   return type_str(t->assoc_base) + "::" + t->assoc_type_name;
    case LogosType::Kind::ImplTrait:   return "impl " + t->struct_name;
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
    const LogosType* make_ref(bool mut, const LogosType* pointee, std::string lifetime = "") {
        LogosType t;
        t.kind = mut ? LogosType::Kind::MutRef : LogosType::Kind::Ref;
        t.pointee = pointee;
        t.lifetime = std::move(lifetime);
        return pool_.alloc(std::move(t));
    }
    const LogosType* make_array(const LogosType* elem, uint64_t n, std::string_view symbolic = "") {
        LogosType t; t.kind = LogosType::Kind::Array;
        t.elem = elem; t.arr_size = n;
        t.arr_size_var = std::string(symbolic);
        return pool_.alloc(std::move(t));
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
    const LogosType* make_trait_object(std::string_view tname) {
        LogosType t; t.kind = LogosType::Kind::TraitObject;
        t.trait_name = std::string(tname);
        return pool_.alloc(std::move(t));
    }
    const LogosType* make_typevar(std::string_view name) {
        LogosType t; t.kind = LogosType::Kind::TypeVar;
        t.type_var_name = std::string(name);
        return pool_.alloc(t);
    }

    const LogosType* lookup_type_by_name(std::string_view name) {
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
        auto tvit = current_type_params_.find(std::string(name));
        if (tvit != current_type_params_.end()) return tvit->second;
        auto ait = type_aliases_.find(std::string(name));
        if (ait != type_aliases_.end()) return ait->second;
        if (structs_.count(std::string(name))) return make_struct_type(name);
        if (classes_.count(std::string(name))) return make_class_type(name);
        if (enums_.count(std::string(name)))   return make_enum_type(name);
        return nullptr;
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
    std::string  cur_package_;
    uint32_t     node_line_ = 0;
    uint32_t     tmp_var_count_ = 0;   // for generating unique internal names

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
    struct Frame {
        std::unordered_map<std::string, VarInfo> vars;  // O(1) lookup
        std::vector<std::string> var_order;              // declaration order
    };
    std::vector<Frame> scope_;

    std::set<std::string> moved_vars_;   // variables consumed by move
    std::set<std::string> copy_types_;   // types with impl Copy — never move-only

    void push_scope() { scope_.emplace_back(); }
    void pop_scope() {
        if (!scope_.empty()) {
            // Remove popped variables from moved set
            for (auto& name : scope_.back().var_order)
                moved_vars_.erase(name);
            scope_.pop_back();
        }
    }

    // Types with Drop (or droppable fields) are move-only (not copyable),
    // unless they explicitly implement Copy.
    bool is_move_type(const LogosType* t) const {
        if (!needs_drop(t)) return false;
        // Copy overrides move semantics.
        std::string name;
        if (t->kind == LogosType::Kind::Struct || t->kind == LogosType::Kind::Class)
            name = t->struct_name;
        return name.empty() || !copy_types_.count(name);
    }

    // Mark a variable as moved (consumed). It will not be dropped at scope exit.
    void mark_moved(const std::string& name) {
        moved_vars_.insert(name);
    }

    // Check if a type has a Drop impl (Type__drop exists in funcs_).
    std::string drop_fn_for(const LogosType* t) const {
        if (!t) return {};
        std::string type_name;
        if (t->kind == LogosType::Kind::Struct) type_name = t->struct_name;
        else if (t->kind == LogosType::Kind::Class) type_name = t->struct_name;
        if (type_name.empty()) return {};
        std::string mangled = type_name + "__drop";
        if (funcs_.count(mangled)) return mangled;
        return {};
    }

    // Check if a struct has droppable fields (even without explicit Drop impl).
    bool has_droppable_fields(const LogosType* t) const {
        if (!t || t->kind != LogosType::Kind::Struct) return false;
        auto sit = structs_.find(t->struct_name);
        if (sit == structs_.end()) return false;
        for (auto& f : sit->second.fields) {
            if (!drop_fn_for(f.type).empty()) return true;
            if (has_droppable_fields(f.type)) return true;
        }
        return false;
    }

    // Check if a type needs any drop action (explicit Drop or droppable fields).
    bool needs_drop(const LogosType* t) const {
        return !drop_fn_for(t).empty() || has_droppable_fields(t);
    }

    // Build SDrop for a variable, handling both explicit Drop and droppable fields.
    std::optional<lir::LStmt> make_drop_stmt(const std::string& name, const VarInfo& info) const {
        auto dfn = drop_fn_for(info.type);
        bool df  = has_droppable_fields(info.type);
        if (dfn.empty() && !df) return std::nullopt;
        return lir::LStmt{node_line_, lir::SDrop{name, dfn, info.type, df}};
    }

    // Collect SDrop statements for current scope frame (reverse declaration order).
    // Skips moved variables.
    std::vector<lir::LStmt> collect_drops() const {
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

    // Collect SDrop statements for ALL enclosing scopes (reverse order, for early return).
    // Skips moved variables.
    std::vector<lir::LStmt> collect_all_drops() const {
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

    void define(std::string_view name, const LogosType* t, bool is_mut = false) {
        if (!scope_.empty()) {
            auto sname = std::string(name);
            if (!scope_.back().vars.count(sname))
                scope_.back().var_order.push_back(sname);
            scope_.back().vars[sname] = {t, is_mut};
        }
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

    // Returns true when a type acts as a pointer/reference (Ptr, Ref, or MutRef).
    static bool is_ref_like(LogosType::Kind k) noexcept {
        return k == LogosType::Kind::Ptr ||
               k == LogosType::Kind::Ref ||
               k == LogosType::Kind::MutRef;
    }

    std::string_view struct_name_of(std::string_view var_name) {
        auto* t = lookup(var_name);
        if (!t) return {};
        if (t->kind == LogosType::Kind::Struct) return t->struct_name;
        if (is_ref_like(t->kind) && t->pointee &&
            t->pointee->kind == LogosType::Kind::Struct)
            return t->pointee->struct_name;
        return {};
    }

    // Returns class name (base name) if the var is a class or *class / &class.
    std::string_view class_name_of(std::string_view var_name) {
        auto* t = lookup(var_name);
        if (!t) return {};
        if (t->kind == LogosType::Kind::Class) return t->struct_name;
        if (is_ref_like(t->kind) && t->pointee &&
            t->pointee->kind == LogosType::Kind::Class)
            return t->pointee->struct_name;
        return {};
    }

    std::string_view struct_name_from_type(const LogosType* t) {
        if (!t) return {};
        if (t->kind == LogosType::Kind::Struct) return t->struct_name;
        if (is_ref_like(t->kind) && t->pointee &&
            t->pointee->kind == LogosType::Kind::Struct)
            return t->pointee->struct_name;
        return {};
    }

    // Returns class name if the type is a class or a pointer/reference to a class.
    std::string_view class_name_from_type(const LogosType* t) {
        if (!t) return {};
        if (t->kind == LogosType::Kind::Class) return t->struct_name;
        if (is_ref_like(t->kind) && t->pointee &&
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
        for (auto& f : it->second.all_fields) {
            if (f.name == fname) return f.type;
            if (f.is_variadic && fname.starts_with(f.name) && fname.size() > f.name.size() + 1 && fname[f.name.size()] == '_')
                return f.type;
        }
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

    struct SemaFieldInfo  { std::string_view name; const LogosType* type; bool is_pub = false; bool is_variadic = false; };
    struct SemaStructInfo { std::vector<SemaFieldInfo> fields; std::vector<TypeParam> type_params;
                            bool is_pub = false; std::string source_file;
                            std::string package; };
    struct SemaFuncInfo   { std::vector<const LogosType*> param_types; const LogosType* ret_type;
                            std::vector<TypeParam> type_params; bool is_vararg = false;
                            bool is_pub = false; bool is_const = false; bool is_unsafe = false;
                            std::string source_file; std::string package; };
    struct SemaVariantInfo{
        std::string_view name; int32_t value;
        std::vector<const LogosType*> payload_types;  // empty = no payload
        bool is_variadic = false;                     // variadic pack payload (...T)
    };
    struct SemaEnumInfo   {
        std::vector<SemaVariantInfo> variants;
        std::vector<TypeParam> type_params;  // for generic enums
    };
    struct ClassFieldInfo { std::string name; const LogosType* type; bool is_pub = false; bool is_variadic = false; };
    struct SemaClassInfo  {
        std::string parent_name;
        std::vector<const LogosType*> parent_type_args;  // type args passed to parent (e.g. [TypeVar(T)])
        bool is_abstract = false;
        std::string package;
        // All fields accessible on this class (parent fields first, then own).
        std::vector<ClassFieldInfo> all_fields;
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
        bool has_default = false;   // trait method has a default body
        bool is_unsafe = false;     // declared unsafe fn in trait
        AnyVal default_ast{};       // AST node for default method (valid when has_default)
    };
    struct SemaAssocTypeInfo {
        std::string name;  // e.g. "Item"
        std::vector<TraitBound> bounds;
    };
    struct SemaTraitInfo {
        std::string name;
        std::vector<TypeParam> type_params;  // e.g. trait Into<T> has T
        std::vector<SemaTraitMethodInfo> methods;
        std::vector<SemaAssocTypeInfo> assoc_types;
    };
    struct SemaImplInfo {
        std::string trait_name;
        std::string target_type;
    };

    // Type params in scope for the function/struct currently being processed.
    // Maps type param name → TypeVar LogosType*.
    std::unordered_map<std::string, const LogosType*> current_type_params_;
    // Set by collect_impl/lower_impl_block for `impl<T>` blocks so that
    // collect_fn/lower_fn include the impl-level type params in the function.
    std::vector<TypeParam> impl_type_params_;

    std::unordered_map<std::string, SemaStructInfo>   structs_;
    // concrete_name (e.g. "Pair__i32") → SemaStructInfo for explicit specializations.
    std::unordered_map<std::string, SemaStructInfo>   struct_specs_sema_;
    std::unordered_map<std::string, SemaEnumInfo>     enums_;
    std::unordered_map<std::string, SemaClassInfo>    classes_;
    std::unordered_map<std::string, SemaFuncInfo>     funcs_;
    std::unordered_map<std::string, SemaFuncInfo>     generic_funcs_; // overloaded generic variants
    // const fn bodies: mangled_name → (body_block, param_names)
    struct ConstFnBody { TinyMapView body; std::vector<std::string> param_names; };
    std::unordered_map<std::string, ConstFnBody>      const_fn_bodies_;
    std::unordered_map<std::string, const LogosType*> type_aliases_;
    std::unordered_map<std::string, const LogosType*> module_consts_;
    std::unordered_map<std::string, SemaTraitInfo>    traits_;
    // "TraitName::TypeName" → impl info
    std::unordered_map<std::string, SemaImplInfo>     impls_;
    // "TraitName::TypeName::AssocName" → assoc type + impl's own type params for substitution.
    // For non-generic impls, impl_type_params is empty and type is concrete.
    // For generic impls (impl<T> Trait for Struct<T>), impl_type_params = [T] and
    // type may contain TypeVar("T"); lookup substitutes T → concrete arg.
    struct AssocTypeEntry {
        const LogosType*       type;
        std::vector<TypeParam> impl_type_params;
    };
    std::unordered_map<std::string, AssocTypeEntry> assoc_type_impls_;

    // Current trait being defined (set during collect_trait for Self::Item resolution)
    std::string current_trait_name_;
    // Bounds per type param name (set alongside current_type_params_ during push_type_params)
    std::unordered_map<std::string, std::vector<TraitBound>> current_type_bounds_;

    // ── Type parameter helpers ────────────────────────────────────

    // Read type_param_list from an AST node that may have TYPE_PARAMS and/or WHERE clause.
    // Collect LIFETIME_PARAM names from a TYPE_PARAMS node (e.g. <'a, 'b, T>).
    std::vector<std::string> read_lifetime_params(TinyMapView node) {
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

    // Read type params from a specific field (TYPE_PARAMS or IMPL_TYPE_PARAMS).
    std::vector<TypeParam> read_type_params_from(TinyMapView node, int32_t field_code) {
        std::vector<TypeParam> result;
        AnyVal tpav = node.get(field_code);
        if (tpav.is_null()) return result;
        auto tplist = map_of(tpav);
        if (!tplist.has_key(la::ITEMS)) return result;
        auto tpitems = arr_of(tplist.get(la::ITEMS.code));
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
                        tp.bounds.push_back(std::move(tb));
                    }
                }
            }
            result.push_back(std::move(tp));
        }
        return result;
    }

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
                        tp.bounds.push_back(std::move(tb));
                    }
                }
            }
            // Validate: variadic param must be last
            if (tp.is_variadic && i + 1 < tpitems.size())
                error("variadic type parameter must be last in the type parameter list");
            result.push_back(std::move(tp));
        }
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

    // Push type params into current_type_params_ (call before resolving fn/struct body).
    void push_type_params(const std::vector<TypeParam>& tps) {
        for (auto& tp : tps) {
            if (tp.is_const) {
                LogosType c; c.kind = LogosType::Kind::ConstVar;
                c.type_var_name = tp.name;
                c.pointee = tp.const_type;
                current_type_params_[tp.name] = pool_.alloc(std::move(c));
            } else {
                current_type_params_[tp.name] = make_typevar(tp.name);
            }
            if (!tp.bounds.empty()) {
                current_type_bounds_[tp.name] = tp.bounds;
            }
        }
    }
    void pop_type_params(const std::vector<TypeParam>& tps) {
        for (auto& tp : tps) {
            current_type_params_.erase(tp.name);
            current_type_bounds_.erase(tp.name);
        }
    }

    // ── Sema-side type substitution (TypeVar → concrete) ────────────

    using SemaSubst = std::unordered_map<std::string, const LogosType*>;

    const LogosType* subst_type_sema(const LogosType* t, const SemaSubst& s) {
        if (!t) return t;
        switch (t->kind) {
        case LogosType::Kind::ConstVar:
        case LogosType::Kind::TypeVar: {
            auto it = s.find(t->type_var_name);
            return (it != s.end()) ? it->second : t;
        }
        case LogosType::Kind::Array: {
            auto* elem = subst_type_sema(t->elem, s);
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
            auto* inner = subst_type_sema(t->pointee, s);
            if (inner == t->pointee) return t;
            return make_ptr(t->mut_ptr, inner);
        }
        case LogosType::Kind::Ref:
        case LogosType::Kind::MutRef: {
            auto* inner = subst_type_sema(t->pointee, s);
            if (inner == t->pointee) return t;
            return make_ref(t->kind == LogosType::Kind::MutRef, inner, t->lifetime);
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
        case LogosType::Kind::Class: {
            if (t->type_args.empty()) return t;
            std::vector<const LogosType*> new_args;
            bool changed = false;
            for (auto* a : t->type_args) {
                auto* na = subst_type_sema(a, s);
                changed |= (na != a);
                new_args.push_back(na);
            }
            if (!changed) return t;
            return make_generic_class(t->struct_name, std::move(new_args));
        }
        case LogosType::Kind::Enum: {
            if (t->type_args.empty()) return t;
            std::vector<const LogosType*> new_args;
            bool changed = false;
            for (auto* a : t->type_args) {
                auto* na = subst_type_sema(a, s);
                changed |= (na != a);
                new_args.push_back(na);
            }
            if (!changed) return t;
            LogosType nt;
            nt.kind = LogosType::Kind::Enum;
            nt.enum_name = t->enum_name;
            nt.type_args = std::move(new_args);
            return pool_.alloc(std::move(nt));
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
        case LogosType::Kind::Closure: {
            std::vector<const LogosType*> new_params;
            bool changed = false;
            for (auto* p : t->closure_params) {
                auto* np = subst_type_sema(p, s);
                changed |= (np != p);
                new_params.push_back(np);
            }
            auto* new_ret = subst_type_sema(t->closure_ret, s);
            changed |= (new_ret != t->closure_ret);
            if (!changed) return t;
            
            LogosType nt;
            nt.kind = LogosType::Kind::Closure;
            nt.closure_params = std::move(new_params);
            nt.closure_ret = new_ret;
            return pool_.alloc(std::move(nt));
        }
        case LogosType::Kind::AssocType: {
            // Substitute the base type first.
            auto* subbed_base = subst_type_sema(t->assoc_base, s);
            
            // Try resolving: if base is substituted to a concrete type, look up impl.
            const LogosType* concrete = nullptr;
            if (subbed_base && subbed_base->kind != LogosType::Kind::TypeVar && subbed_base->kind != LogosType::Kind::ConstVar) {
                concrete = subbed_base;
            } else if (subbed_base && subbed_base->kind == LogosType::Kind::TypeVar) {
                 // Even if it's a typevar, perhaps it is a known concrete name like "i32" (though unlikely for TypeVar)
                 // actually if it's still a typevar, we can't resolve it yet.
            }
            // we should lookup concrete by name if it's still not found, but it should already be subbed.
            if (!concrete && t->assoc_base->kind == LogosType::Kind::TypeVar) {
               // Not in subst map; maybe it's a concrete type name (like "i32")
               concrete = const_cast<SemaChecker*>(this)->lookup_type_by_name(t->assoc_base->type_var_name);
            }

            if (concrete) {
                std::string concrete_name = type_str(concrete);
                // 1. Direct lookup (non-generic impls: key stored under concrete name).
                std::string key = t->trait_name + "::" + concrete_name + "::" + t->assoc_type_name;
                auto ait = assoc_type_impls_.find(key);
                if (ait != assoc_type_impls_.end()) {
                    // Re-run substitution so nested associated types collapse fully
                    // (e.g. T::X -> S::Y -> i32), not just one lookup step.
                    return subst_type_sema(ait->second.type, {});
                }
                // 2. Base-name fallback (generic impls).
                std::string base_name = (concrete->kind == LogosType::Kind::Struct || concrete->kind == LogosType::Kind::Class)
                                        ? concrete->struct_name : "";
                if (!base_name.empty() && base_name != concrete_name) {
                    std::string base_key = t->trait_name + "::" + base_name
                                          + "::" + t->assoc_type_name;
                    auto ait2 = assoc_type_impls_.find(base_key);
                    if (ait2 != assoc_type_impls_.end()) {
                        auto& entry = ait2->second;
                        if (entry.impl_type_params.empty()) return subst_type_sema(entry.type, {});
                        SemaSubst impl_subst;
                        for (size_t i = 0; i < entry.impl_type_params.size() &&
                                           i < concrete->type_args.size(); ++i)
                            impl_subst[entry.impl_type_params[i].name] = concrete->type_args[i];
                        return subst_type_sema(entry.type, impl_subst);
                    }
                }
            }
            if (subbed_base != t->assoc_base) {
                LogosType nt = *t;
                nt.assoc_base = subbed_base;
                return pool_.alloc(std::move(nt));
            }
            return t;
        }
        default: return t;
        }
    }

    // ── Compatibility with class hierarchy ───────────────────────
    // Use this inside SemaChecker instead of static types_compatible when
    // class pointer upcasting should be allowed.
    bool compat(const LogosType* from, const LogosType* to) const {
        if (types_compatible(from, to)) return true;
        // *mut/const Derived / &Derived / &mut Derived compatible with *const/mut Base (upcast)
        if (from && to &&
            is_ref_like(from->kind) && is_ref_like(to->kind) &&
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

        if (tc == la::LIT_INT) {
            auto sv = str_of(node.get(la::VALUE.code));
            LogosType t; t.kind = LogosType::Kind::IntLit;
            t.const_val = (int64_t)std::strtoll(sv.data(), nullptr, 10);
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
                if (av.is_value()) {
                    auto sv = str_of(av);
                    // If sv starts with a digit, it's a literal size.
                    if (!sv.empty() && std::isdigit(sv[0])) {
                        n = (uint64_t)std::strtoull(sv.data(), nullptr, 10);
                    } else {
                        // Otherwise, it might be a symbolic constant parameter.
                        symbolic = std::string(sv);
                    }
                } else if (av.is_pointer()) {
                    // Safety fallback: if it's somehow a string object
                    auto sv = str_of(av);
                    if (!sv.empty() && std::isdigit(sv[0])) {
                        n = (uint64_t)std::strtoull(sv.data(), nullptr, 10);
                    } else {
                        symbolic = std::string(sv);
                    }
                }
            }
            return make_array(elem, n, symbolic);
        }

        if (tc == la::ASSOC_TYPE_REF) {
            // base::Item — associated type reference
            auto* base_type = resolve_type(map_of(node.get(la::RECEIVER.code)));
            auto assoc      = std::string(str_of(node.get(la::FIELD.code)));  // "Item"
            std::string trait_for_assoc;

            if (base_type->kind == LogosType::Kind::TypeVar) {
                auto& tp_name = base_type->type_var_name;
                if (tp_name == "Self" && !current_trait_name_.empty()) {
                    trait_for_assoc = current_trait_name_;
                } else {
                    auto bit = current_type_bounds_.find(tp_name);
                    if (bit != current_type_bounds_.end()) {
                        for (auto& bound : bit->second) {
                            auto tit = traits_.find(bound.trait_name);
                            if (tit != traits_.end()) {
                                for (auto& at : tit->second.assoc_types) {
                                    if (at.name == assoc) { trait_for_assoc = bound.trait_name; break; }
                                }
                            }
                            if (!trait_for_assoc.empty()) break;
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
                // Check all traits for ANY type that might have this assoc type (last resort lookup)
                std::string cname = type_str(base_type);
                for (auto& [tname, tinfo] : traits_) {
                    if (impls_.count(tname + "::" + cname)) {
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
            LogosType t;
            t.kind            = LogosType::Kind::AssocType;
            t.assoc_base      = base_type;
            t.trait_name      = trait_for_assoc;
            t.assoc_type_name = assoc;

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

        if (tc == la::TYPE_REF) {
            auto name = str_of(node.get(la::NAME.code));
            auto* t = lookup_type_by_name(name);
            if (t) return t;
            error(std::format("unknown type '{}'", name));
            return error_t();
        }

        if (tc == la::GENERIC_INST) {
            auto name = str_of(node.get(la::NAME.code));
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
            bool is_class  = classes_.count(std::string(name)) > 0;
            bool is_enum   = enums_.count(std::string(name)) > 0;
            if (!is_struct && !is_class && !is_enum) {
                error(std::format("unknown generic type '{}'", name));
                return error_t();
            }
            // Resolve each type arg (TypeVars in current scope are expanded).
            // Skip LIFETIME_PARAM items ('a) — lifetimes are erased at runtime.
            std::vector<const LogosType*> args;
            if (node.has_key(la::ITEMS)) {
                auto items = arr_of(node.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i) {
                    auto item = map_of(items.get(i));
                    if (code_of(item) == la::LIFETIME_PARAM) continue;
                    args.push_back(resolve_type(item));
                }
            }
            if (is_enum) {
                auto eit = enums_.find(std::string(name));
                if (eit != enums_.end()) check_type_bounds(std::string(name), eit->second.type_params, args);
                LogosType t; t.kind = LogosType::Kind::Enum;
                t.enum_name = std::string(name);
                t.type_args = std::move(args);
                return pool_.alloc(std::move(t));
            }
            if (is_class) {
                auto cit = classes_.find(std::string(name));
                if (cit != classes_.end()) check_type_bounds(std::string(name), cit->second.type_params, args);
                if (args.empty()) return make_class_type(name);
                return make_generic_class(name, std::move(args));
            }
            auto sit = structs_.find(std::string(name));
            if (sit != structs_.end()) check_type_bounds(std::string(name), sit->second.type_params, args);
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
        // Intermediate pass: type aliases and consts (Phase 2). Wait, we execute this FIRST so aliases are known for fn signatures.
        for (size_t ai = 0; ai < asts.size(); ++ai) {
            holder_ = asts[ai].holder();
            file_ = (filenames_ && ai < filenames_->size()) ? (*filenames_)[ai] : std::string{};
            auto root = asts[ai].root_object().as_tiny_map();
            cur_package_ = read_package_name(root);
            collect_module(root, 2);
        }
        // Second pass: fill in fields, variants, function signatures (Phase 1).
        for (size_t ai = 0; ai < asts.size(); ++ai) {
            holder_ = asts[ai].holder();
            file_ = (filenames_ && ai < filenames_->size()) ? (*filenames_)[ai] : std::string{};
            auto root = asts[ai].root_object().as_tiny_map();
            cur_package_ = read_package_name(root);
            collect_module(root, 1);
        }
        cur_package_ = {};

        // Third pass: build class inheritance (all_fields + full vtable_order).
        finalize_classes();

        // Final pass: simplify all collected types (resolve concrete associated types).
        simplify_all_types();
    }

    void simplify_all_types() {
        for (auto& [name, info] : structs_) {
            for (auto& f : info.fields) f.type = subst_type_sema(f.type, {});
        }
        for (auto& [name, info] : classes_) {
            for (auto& f : info.all_fields) f.type = subst_type_sema(f.type, {});
        }
        for (auto& [name, info] : enums_) {
            for (auto& v : info.variants) {
                for (auto& pt : v.payload_types) pt = subst_type_sema(pt, {});
            }
        }
        auto simplify_fn = [&](SemaFuncInfo& fi) {
            for (auto& pt : fi.param_types) pt = subst_type_sema(pt, {});
            fi.ret_type = subst_type_sema(fi.ret_type, {});
        };
        for (auto& [name, info] : funcs_) simplify_fn(info);
        for (auto& [name, info] : generic_funcs_) simplify_fn(info);
        for (auto& [name, t] : type_aliases_)   t = subst_type_sema(t, {});
        for (auto& [name, t] : module_consts_)   t = subst_type_sema(t, {});
    }

    // Extract the package name from a module root node (la::NAME field).
    std::string read_package_name(TinyMapView mod) {
        if (!mod.has_key(la::NAME)) return {};
        return std::string(str_of(mod.get(la::NAME.code)));
    }

    // Report error if item is private and caller is in a different package.
    void check_pub_access(bool is_pub, const std::string& def_package,
                          std::string_view item_name) {
        if (is_pub || def_package.empty() || cur_package_.empty()) return;
        if (def_package != cur_package_)
            error(std::format("'{}' is private to package '{}'", item_name, def_package));
    }

    void check_type_bounds(const std::string& target_name,
                           const std::vector<TypeParam>& type_params,
                           const std::vector<const LogosType*>& args) {
        if (type_params.empty()) return;
        bool has_variadic = type_params.back().is_variadic;
        size_t non_variadic_count = type_params.size() - (has_variadic ? 1 : 0);

        for (size_t i = 0; i < args.size(); ++i) {
            if (i >= type_params.size() && !has_variadic) break;

            const auto& tp = (has_variadic && i >= non_variadic_count)
                             ? type_params.back()
                             : type_params[i];

            auto* concrete = args[i];
            if (!concrete || concrete->kind == LogosType::Kind::Error) continue;
            if (concrete->kind == LogosType::Kind::TypeVar) continue; // defer until mono

            std::string concrete_str = type_str(concrete);
            std::string unwrapped_name;
            if ((concrete->kind == LogosType::Kind::Ptr || concrete->kind == LogosType::Kind::Ref || concrete->kind == LogosType::Kind::MutRef) && concrete->pointee) {
                auto* inner = concrete->pointee;
                if (inner->kind == LogosType::Kind::Class)
                    unwrapped_name = concrete_class_name(inner);
                else if (inner->kind == LogosType::Kind::Struct)
                    unwrapped_name = concrete_struct_name(inner);
            } else if (concrete->kind == LogosType::Kind::Struct) {
                unwrapped_name = concrete_struct_name(concrete);
            } else if (concrete->kind == LogosType::Kind::Class) {
                unwrapped_name = concrete_class_name(concrete);
            }

            for (auto& bound : tp.bounds) {
                auto key1 = bound.trait_name + "::" + concrete_str;
                auto key2 = unwrapped_name.empty() ? "" : bound.trait_name + "::" + unwrapped_name;
                if (!impls_.count(key1) && (key2.empty() || !impls_.count(key2))) {
                    error(std::format("'{}': type '{}' does not implement trait '{}' required by parameter '{}'",
                          target_name, concrete_str, bound.trait_name, tp.name));
                }
            }
        }
    }

    void collect_module(TinyMapView mod, int phase) {
        if (!mod.has_key(la::ITEMS)) return;
        auto items = arr_of(mod.get(la::ITEMS.code));
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto item = map_of(items.get(i));
            int32_t c = code_of(item);
            if (phase == 1) {
                if      (c == la::STRUCT) {
                    if (is_specialization_struct(item)) collect_struct_spec(item);
                    else                                collect_struct(item);
                } else if (c == la::ENUM)                       collect_enum(item);
                else if (c == la::CLASS)                        collect_class(item);
                else if (c == la::FN || c == la::EXTERN_FN)   collect_fn(item);
                else if (c == la::TRAIT_DEF)                  collect_trait(item);
                else if (c == la::IMPL_BLOCK)                 collect_impl(item);
            } else {
                if      (c == la::TYPE_ALIAS)                 collect_type_alias(item);
                else if (c == la::CONST_DEF)                  collect_const(item);
            }
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
                        bool is_var = false;
                        if (v.has_key(la::IS_VARIADIC)) is_var = v.get(la::IS_VARIADIC.code).as_value<int32_t>() != 0;

                        if (v.has_key(la::ITEMS)) {
                            auto av = v.get(la::ITEMS.code);
                            if (is_var) {
                                // Single type_ref map (variadic variant: ITEMS: $4)
                                payload.push_back(resolve_type(map_of(av)));
                            } else {
                                // Nested record { ITEMS: [...] } (normal variant: ITEMS: $3)
                                // or raw array (old grammar/other paths)
                                TinyMapView tm(av.to_offset(), holder_);
                                ArrayView pitems;
                                if (tm.has_key(la::ITEMS)) {
                                    pitems = arr_of(tm.get(la::ITEMS.code));
                                } else {
                                    pitems = arr_of(av);
                                }
                                for (uint64_t j = 0; j < pitems.size(); ++j)
                                    payload.push_back(resolve_type(map_of(pitems.get(j))));
                            }
                        }
                        info.variants.push_back({vname, vval, std::move(payload), is_var});
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
        current_trait_name_ = tname;
        SemaTraitInfo info;
        info.name = tname;
        // Read trait type params (e.g. trait Into<T>)
        info.type_params = read_type_params(node);
        push_type_params(info.type_params);
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto m = map_of(items.get(i));
                if (code_of(m) == la::ASSOC_TYPE_DEF) {
                    SemaAssocTypeInfo at;
                    at.name = std::string(str_of(m.get(la::NAME.code)));
                    if (m.has_key(la::ITEMS)) {
                        auto bounds = arr_of(m.get(la::ITEMS.code));
                        for (uint64_t b = 0; b < bounds.size(); ++b) {
                            auto bnode = map_of(bounds.get(b));
                            if (code_of(bnode) == la::TRAIT_BOUND) {
                                TraitBound tb;
                                tb.trait_name = std::string(str_of(bnode.get(la::NAME.code)));
                                at.bounds.push_back(std::move(tb));
                            }
                        }
                    }
                    info.assoc_types.push_back(std::move(at));
                    continue;
                }
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
                mi.has_default = m.has_key(la::BODY);
                if (mi.has_default)
                    mi.default_ast = items.get(i);
                if (m.has_key(la::IS_UNSAFE)) {
                    AnyVal av = m.get(la::IS_UNSAFE.code);
                    mi.is_unsafe = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
                }
                info.methods.push_back(std::move(mi));
            }
        }
        pop_type_params(info.type_params);
        current_type_params_.erase("Self");
        current_trait_name_.clear();
        traits_[tname] = std::move(info);
    }

    void collect_impl(TinyMapView node) {
        std::string trait_name;
        if (node.has_key(la::NAME))
            trait_name = std::string(str_of(node.get(la::NAME.code)));
        // Push impl's own type params: either from IMPL_TYPE_PARAMS (new generic trait impl
        // form: impl<T> Trait for Struct<T>) or from TYPE_PARAMS (standalone: impl<T> Pair<T>).
        std::vector<TypeParam> impl_tps;
        if (node.has_key(la::IMPL_TYPE_PARAMS)) {
            impl_tps = read_type_params_from(node, la::IMPL_TYPE_PARAMS.code);
            push_type_params(impl_tps);
            impl_type_params_ = impl_tps;
        } else if (trait_name.empty() && node.has_key(la::TYPE_PARAMS)) {
            impl_tps = read_type_params(node);
            push_type_params(impl_tps);
            impl_type_params_ = impl_tps;  // so collect_fn includes them in fn.type_params
        }
        // TYPE is the target type (simple_type, ptr_type, or GENERIC_INST)
        std::string target;
        if (node.has_key(la::TYPE)) {
            auto tnode = map_of(node.get(la::TYPE.code));
            if (code_of(tnode) == la::PTR_TYPE) {
                // *const T or *mut T → resolve full type string
                auto* resolved = resolve_type(tnode);
                target = type_str(resolved);
            } else if (code_of(tnode) == la::GENERIC_INST) {
                // Concrete generic (e.g. Pair<i32>) → use mangled name; generic (Pair<T>) → base name.
                target = std::string(str_of(tnode.get(la::NAME.code)));
                if (impl_tps.empty()) {
                    // No own type params — may be a concrete specialization like impl Pair<i32>.
                    auto* resolved = resolve_type(tnode);
                    if (resolved && !resolved->type_args.empty()) {
                        bool concrete = true;
                        for (auto* a : resolved->type_args)
                            if (a && a->kind == LogosType::Kind::TypeVar) { concrete = false; break; }
                        if (concrete) {
                            if (resolved->kind == LogosType::Kind::Struct)
                                target = concrete_struct_name(resolved);
                            else if (resolved->kind == LogosType::Kind::Class)
                                target = concrete_class_name(resolved);
                        }
                    }
                }
            } else {
                target = std::string(str_of(tnode.get(la::NAME.code)));
            }
        }
        // Note: impl_tps are left in current_type_params_ until after collect_fn calls below.
        if (trait_name.empty())
            ctx_ = std::format("impl {}", target);
        else
            ctx_ = std::format("impl {} for {}", trait_name, target);
        // Verify trait exists (only for trait impls)
        if (!trait_name.empty() && !traits_.count(trait_name))
            error(std::format("impl: unknown trait '{}'", trait_name));
        // Resolve trait type args (e.g. impl Into<i32> for Celsius → T=i32)
        // and push them into current_type_params_ so method sigs resolve correctly.
        std::vector<const LogosType*> trait_type_args;
        if (!trait_name.empty() && node.has_key(la::TYPE_PARAMS)) {
            AnyVal tpav = node.get(la::TYPE_PARAMS.code);
            if (!tpav.is_null()) {
                auto tplist = map_of(tpav);
                if (tplist.has_key(la::ITEMS)) {
                    auto items = arr_of(tplist.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < items.size(); ++i)
                        trait_type_args.push_back(resolve_type(map_of(items.get(i))));
                }
            }
            auto tit = traits_.find(trait_name);
            if (tit != traits_.end()) {
                for (size_t i = 0; i < tit->second.type_params.size() &&
                                    i < trait_type_args.size(); ++i)
                    current_type_params_[tit->second.type_params[i].name] = trait_type_args[i];
            }
        }
        // Register impl methods as free functions with mangled names: Target__method
        // Also collect associated type definitions.
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
                } else if (code_of(m) == la::ASSOC_TYPE_IMPL && !trait_name.empty()) {
                    auto aname = std::string(str_of(m.get(la::NAME.code)));
                    auto* atype = resolve_type(map_of(m.get(la::TYPE.code)));
                    std::string key = trait_name + "::" + target + "::" + aname;
                    assoc_type_impls_[key] = { atype, impl_tps };
                }
            }
        }
        // Check completeness: every required trait method must be in the impl.
        // Default methods are registered as Target__method if not overridden.
        if (!trait_name.empty()) {
            auto tit = traits_.find(trait_name);
            if (tit != traits_.end()) {
                for (auto& m : tit->second.methods) {
                    auto mangled = target + "__" + m.name;
                    if (funcs_.count(mangled)) {
                        auto& impl_fn = funcs_[mangled];
                        if (m.is_unsafe != impl_fn.is_unsafe) {
                            error(std::format("impl {} for {}: method '{}' has mismatched unsafe parity (trait: {}, impl: {})",
                                trait_name, target, m.name, 
                                m.is_unsafe ? "unsafe" : "safe", 
                                impl_fn.is_unsafe ? "unsafe" : "safe"));
                        }
                    }
                    if (!funcs_.count(mangled)) {
                        if (m.has_default) {
                            // Register default method as Target__method.
                            // Push Self → target type so parameter types resolve correctly.
                            // Build Self type; for generic impls include the type params as TypeVars.
                            const LogosType* self_type = nullptr;
                            if (structs_.count(target)) {
                                if (!impl_tps.empty()) {
                                    std::vector<const LogosType*> tv_args;
                                    for (auto& tp : impl_tps)
                                        tv_args.push_back(make_typevar(tp.name));
                                    self_type = make_generic_struct(target, std::move(tv_args));
                                } else {
                                    self_type = make_struct_type(target);
                                }
                            } else if (classes_.count(target)) {
                                self_type = make_ptr(true, make_class_type(target));
                            }
                            if (self_type)
                                current_type_params_["Self"] = self_type;
                            collect_fn(map_of(m.default_ast), target);
                            current_type_params_.erase("Self");
                        } else {
                            error(std::format("impl {} for {}: missing method '{}'",
                                  trait_name, target, m.name));
                        }
                    }
                }
            }
        }
        // Check associated type completeness
        if (!trait_name.empty()) {
            auto tit = traits_.find(trait_name);
            if (tit != traits_.end()) {
                for (auto& at : tit->second.assoc_types) {
                    std::string key = trait_name + "::" + target + "::" + at.name;
                    if (!assoc_type_impls_.count(key))
                        error(std::format("impl {} for {}: missing associated type '{}'",
                              trait_name, target, at.name));
                }
            }
        }
        // Register Copy types so is_move_type() can respect them.
        if (trait_name == "Copy" && !target.empty())
            copy_types_.insert(target);
        // Clean up trait type params from scope
        if (!trait_name.empty() && !trait_type_args.empty()) {
            auto tit = traits_.find(trait_name);
            if (tit != traits_.end()) {
                for (auto& tp : tit->second.type_params)
                    current_type_params_.erase(tp.name);
            }
        }
        // Clean up impl's own type params (pushed at top for standalone generic impl)
        if (!impl_tps.empty()) { pop_type_params(impl_tps); impl_type_params_.clear(); }
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
            info.package = cur_package_;
            if (node.has_key(la::FIELDS)) {
                auto fields = arr_of(node.get(la::FIELDS.code));
                for (uint64_t i = 0; i < fields.size(); ++i) {
                    auto fnode = map_of(fields.get(i));
                    auto fname = str_of(fnode.get(la::NAME.code));
                    auto ftype = resolve_type(map_of(fnode.get(la::TYPE.code)));
                    bool fpub = fnode.has_key(la::IS_PUB) &&
                                fnode.get(la::IS_PUB.code).is_value() &&
                                fnode.get(la::IS_PUB.code).as_value<uint8_t>() != 0;
                    info.fields.push_back({fname, ftype, fpub});
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
        info.package = cur_package_;

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
                    bool fpub = m.has_key(la::IS_PUB) &&
                                m.get(la::IS_PUB.code).is_value() &&
                                m.get(la::IS_PUB.code).as_value<uint8_t>() != 0;
                    bool fvar = false;
                    if (m.has_key(la::IS_VARIADIC)) {
                        AnyVal av = m.get(la::IS_VARIADIC.code);
                        fvar = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
                    }
                    // Own fields (all_fields built in finalize_classes)
                    info.all_fields.push_back({std::string(fname), ftype, fpub, fvar});

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
        info.package = cur_package_;
        push_type_params(info.type_params);
        if (node.has_key(la::FIELDS)) {
            auto fields = arr_of(node.get(la::FIELDS.code));
            for (uint64_t i = 0; i < fields.size(); ++i) {
                auto fnode = map_of(fields.get(i));
                auto fname = str_of(fnode.get(la::NAME.code));
                auto ftype = resolve_type(map_of(fnode.get(la::TYPE.code)));
                bool fpub = fnode.has_key(la::IS_PUB) &&
                            fnode.get(la::IS_PUB.code).is_value() &&
                            fnode.get(la::IS_PUB.code).as_value<uint8_t>() != 0;
                bool fvar = false;
                if (fnode.has_key(la::IS_VARIADIC)) {
                    AnyVal av = fnode.get(la::IS_VARIADIC.code);
                    fvar = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
                }
                info.fields.push_back({fname, ftype, fpub, fvar});
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
        if (tc == la::PTR_TYPE || tc == la::REF_TYPE || tc == la::MUT_REF_TYPE) {
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
                        if (tc == la::LIFETIME_PARAM) continue;  // skip — deferred to borrow checker
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

                        if (tc == la::LIFETIME_PARAM) continue;  // skip — deferred to borrow checker
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

        SemaFuncInfo info;
        info.type_params = read_type_params(node);
        // Allow overloading: a non-generic base case and a generic version
        // can coexist with the same name (for variadic recursion base cases).
        if (funcs_.count(mangled)) {
            bool new_is_generic = !info.type_params.empty();
            bool old_is_generic = !funcs_[mangled].type_params.empty();
            if (new_is_generic == old_is_generic) {
                error(std::format("duplicate function '{}'", mangled));
                return;
            }
            // Store the generic version separately; non-generic stays in funcs_.
            if (new_is_generic) {
                // Continue with collection, but store under generic_funcs_
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
                                auto pt = p.has_key(la::TYPE)
                                    ? resolve_type(map_of(p.get(la::TYPE.code))) : error_t();
                                info.param_types.push_back(pt);
                            }
                        }
                    }
                }
                info.ret_type = node.has_key(la::RET_TYPE)
                    ? resolve_type(map_of(node.get(la::RET_TYPE.code))) : void_t();
                if (node.has_key(la::IS_PUB)) {
                    AnyVal av = node.get(la::IS_PUB.code);
                    info.is_pub = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
                }
                if (node.has_key(la::IS_UNSAFE)) {
                    AnyVal av = node.get(la::IS_UNSAFE.code);
                    info.is_unsafe = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
                }
                if (code_of(node) == la::EXTERN_FN) {
                    info.is_pub = true;
                    info.is_unsafe = true;
                }
                if (node.has_key(la::IS_CONST)) {
                    AnyVal av = node.get(la::IS_CONST.code);
                    info.is_const = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
                }
                pop_type_params(info.type_params);
                if (!impl_type_params_.empty()) {
                    auto combined = impl_type_params_;
                    combined.insert(combined.end(), info.type_params.begin(), info.type_params.end());
                    info.type_params = std::move(combined);
                }
                generic_funcs_[mangled] = std::move(info);
                return;
            }
            // else: new is non-generic, old is generic — move old to generic_funcs_
            generic_funcs_[mangled] = std::move(funcs_[mangled]);
            funcs_.erase(mangled);
        }
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
        // Visibility: pub fn → is_pub = true; extern fn is always pub (C FFI).
        // extern fn is also always unsafe (FFI calls require unsafe context, like Rust).
        if (code_of(node) == la::EXTERN_FN) {
            info.is_pub = true;
            info.is_unsafe = true;
        } else if (node.has_key(la::IS_PUB)) {
            AnyVal av = node.get(la::IS_PUB.code);
            info.is_pub = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
        }
        // const fn marker
        if (node.has_key(la::IS_CONST)) {
            AnyVal av = node.get(la::IS_CONST.code);
            info.is_const = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
        }
        // unsafe fn marker
        if (node.has_key(la::IS_UNSAFE)) {
            AnyVal av = node.get(la::IS_UNSAFE.code);
            info.is_unsafe = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
        }
        info.source_file = file_;
        info.package = cur_package_;
        pop_type_params(info.type_params);
        // Prepend impl-level type params AFTER the push/pop cycle so they remain
        // in current_type_params_ (managed by collect_impl's push/pop).
        if (!impl_type_params_.empty()) {
            auto combined = impl_type_params_;
            combined.insert(combined.end(), info.type_params.begin(), info.type_params.end());
            info.type_params = std::move(combined);
        }
        // Store body AST for const fn evaluation.
        bool became_const = info.is_const;
        if (became_const && node.has_key(la::BODY)) {
            ConstFnBody cfb;
            cfb.body = map_of(node.get(la::BODY.code));
            // Read parameter names.
            if (node.has_key(la::PARAMS)) {
                auto params_av = node.get(la::PARAMS.code);
                if (params_av.is_pointer()) {
                    auto params_node = map_of(params_av);
                    if (params_node.has_key(la::ITEMS)) {
                        auto arr = arr_of(params_node.get(la::ITEMS.code));
                        for (uint64_t i = 0; i < arr.size(); ++i) {
                            auto p = map_of(arr.get(i));
                            if (code_of(p) != la::PARAM) continue;
                            cfb.param_names.push_back(std::string(str_of(p.get(la::NAME.code))));
                        }
                    }
                }
            }
            const_fn_bodies_[mangled] = std::move(cfb);
        }
        funcs_[mangled] = std::move(info);  // must come after body storage (info.is_const read above)
    }

    // ── Loop depth / return type ─────────────────────────────────

    int loop_depth_ = 0;
    bool inside_unsafe_ = false;
    const LogosType* ret_type_ = nullptr;
    // Set to true only when lowering a match that is the last statement
    // of a function body (so EXPR arms should be returned, not discarded).
    bool match_in_tail_position_ = false;
    const LogosType* impl_ret_type_inferred_ = nullptr;  // set when lowering impl Trait return fn
    // Type hint for enum literal construction (set from let annotation or return type)
    const LogosType* hint_enum_type_ = nullptr;
    // Type hint for generic struct literal construction (set from let annotation)
    const LogosType* hint_struct_type_ = nullptr;

    // ── Return reachability (on AST nodes) ───────────────────────

    bool stmt_always_returns(TinyMapView stmt) {
        int32_t c = code_of(stmt);
        if (c == la::RETURN) return true;
        if (c == la::UNSAFE_BLOCK) {
            return stmt.has_key(la::BODY) &&
                   block_always_returns(map_of(stmt.get(la::BODY.code)));
        }
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
                } else if (arm.has_key(la::EXPR)) {
                    // Expression arm (pattern => expr,) always provides a value.
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
        for (auto& f : sit->second.fields) {
            if (f.name == fname) return f.type;
            if (f.is_variadic && fname.starts_with(f.name) && fname.size() > f.name.size() + 1 && fname[f.name.size()] == '_')
                return f.type;
        }
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
                for (auto& f : spec_it->second.fields) {
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
            auto sit = structs_.find(struct_t->struct_name);
            if (sit != structs_.end()) {
                for (auto& f : sit->second.fields) {
                    if (f.is_variadic && fname.starts_with(f.name) && fname.size() > f.name.size() + 1 && fname[f.name.size()] == '_') {
                        size_t idx = std::stoull(std::string(fname.substr(f.name.size() + 1)));
                        if (f.type && f.type->kind == LogosType::Kind::TypeVar) {
                            for (size_t i = 0, arg_idx = 0; i < sit->second.type_params.size(); ++i) {
                                if (sit->second.type_params[i].is_variadic) {
                                    if (sit->second.type_params[i].name == f.type->type_var_name) {
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

        auto sit = structs_.find(struct_t->struct_name);
        if (sit == structs_.end()) return raw;
        SemaSubst subst;
        auto& tps = sit->second.type_params;
        for (size_t i = 0, j = 0; i < tps.size() && j < struct_t->type_args.size(); ++i) {
            if (tps[i].is_variadic) break;
            subst[tps[i].name] = struct_t->type_args[j++];
        }
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
            if (moved_vars_.count(std::string(name)))
                error(std::format("use of moved variable '{}'", name));
            return make_expr(t, lir::EVarRef{std::string(name)});
        }

        case la::PACK_EXPAND: {
            auto name = str_of(expr.get(la::NAME.code));
            // Type is the variadic TypeVar — mono will expand this
            auto* t = lookup(name);
            if (!t) {
                error(std::format("pack expand: undefined variable '{}'", name));
                return error_expr();
            }
            return make_expr(t, lir::EPackExpand{std::string(name)});
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
            // Reject aggregate-to-primitive casts (struct/class/array/tuple/enum → scalar).
            if (inner->type && target &&
                inner->type->kind != LogosType::Kind::Error &&
                target->kind != LogosType::Kind::Error) {
                bool src_agg = inner->type->kind == LogosType::Kind::Struct ||
                               inner->type->kind == LogosType::Kind::Class  ||
                               inner->type->kind == LogosType::Kind::Array  ||
                               inner->type->kind == LogosType::Kind::Tuple  ||
                               inner->type->kind == LogosType::Kind::Enum;
                bool tgt_scalar = target->kind == LogosType::Kind::I32  ||
                                  target->kind == LogosType::Kind::I64  ||
                                  target->kind == LogosType::Kind::U8   ||
                                  target->kind == LogosType::Kind::I8   ||
                                  target->kind == LogosType::Kind::U32  ||
                                  target->kind == LogosType::Kind::U64  ||
                                  target->kind == LogosType::Kind::F64  ||
                                  target->kind == LogosType::Kind::Bool ||
                                  target->kind == LogosType::Kind::Ptr;
                if (src_agg && tgt_scalar)
                    error(std::format("cannot cast '{}' to '{}'",
                          type_str(inner->type), type_str(target)));
            }
            return make_expr(target, lir::ECast{std::move(inner)});
        }

        case la::BINOP:       return lower_binop(expr);
        case la::UNARY:       return lower_unary(expr);
        case la::DEREF:       return lower_deref(expr);

        case la::ADDR_OF_MUT: {
            // &mut var — exclusive mutable reference
            auto child = map_of(expr.get(la::VALUE.code));
            if (code_of(child) != la::VAR_REF) {
                error("'&mut' operand must be a variable");
                return error_expr();
            }
            auto var_name = str_of(child.get(la::NAME.code));
            auto* vt = lookup(var_name);
            if (!vt) {
                error(std::format("'&mut': undefined variable '{}'", var_name));
                return error_expr();
            }
            // For arrays, produce &mut elem (reference to first element)
            if (vt->kind == LogosType::Kind::Array)
                return make_expr(make_ref(true, vt->elem), lir::EAddrOf{std::string(var_name)});
            return make_expr(make_ref(true, vt), lir::EAddrOf{std::string(var_name)});
        }
        case la::TRY_EXPR: {
            // expr? — extract Ok(v) or early-return Err(e).
            // Requires: inner : Result<T, E>, current fn return type : Result<?, E>.
            auto inner = expr.has_key(la::VALUE)
                ? lower_expr(map_of(expr.get(la::VALUE.code)))
                : error_expr();
            auto* inner_t = inner->type;
            if (inner_t->kind != LogosType::Kind::Enum || inner_t->enum_name != "Result"
                || inner_t->type_args.size() < 2) {
                error("'?' operator requires a Result<T, E> expression");
                return error_expr();
            }
            if (!ret_type_ || ret_type_->kind != LogosType::Kind::Enum
                || ret_type_->enum_name != "Result") {
                error("'?' operator used in function that does not return Result<T, E>");
                return error_expr();
            }
            // Find Ok and Err discriminants from the enum definition.
            int32_t ok_disc = 0, err_disc = 1;  // default: Ok first, Err second
            auto eit = enums_.find("Result");
            if (eit != enums_.end()) {
                for (auto& v : eit->second.variants) {
                    if (v.name == "Ok")  ok_disc  = v.value;
                    if (v.name == "Err") err_disc = v.value;
                }
            }
            auto* ok_type = inner_t->type_args[0];  // T
            return make_expr(ok_type, lir::ETry{std::move(inner), ok_disc, err_disc});
        }

        case la::CALL:         return lower_call(expr);
        case la::GENERIC_CALL: return lower_generic_call(expr);
        case la::METHOD_CALL:  return lower_method_call(expr);
        case la::STATIC_CALL:  return lower_static_call(expr);
        case la::FIELD_READ:  return lower_field_read(expr);
        case la::STRUCT_LIT:  return lower_struct_lit(expr);
        case la::INDEX_READ:  return lower_index_read(expr);
        case la::ARR_LIT:      return lower_arr_lit(expr);
        case la::ARR_FILL_LIT: return lower_arr_fill_lit(expr);
        case la::ENUM_LIT:    return lower_enum_lit(expr);
        case la::ENUM_LIT_DATA: return lower_enum_lit_data(expr);
        case la::NEW_EXPR:    return lower_new_expr(expr);
        case la::IF:          return lower_if_expr(expr);
        case la::MATCH:       return lower_match_expr(expr);
        case la::CLOSURE_EXPR: return lower_closure_expr(expr);

        case la::UNSAFE_BLOCK: {
            if (!expr.has_key(la::BODY)) return error_expr();
            auto inner = map_of(expr.get(la::BODY.code));
            bool was = inside_unsafe_;
            inside_unsafe_ = true;
            lir::LExprPtr result = nullptr;
            auto block = std::make_unique<lir::LBlock>();
            if (inner.has_key(la::ITEMS)) {
                auto stmts = arr_of(inner.get(la::ITEMS.code));
                for (uint64_t i = 0; i < stmts.size(); ++i) {
                    auto s = map_of(stmts.get(i));
                    if (i == stmts.size() - 1) {
                        int32_t lc = code_of(s);
                        if (lc == la::EXPR_STMT && s.has_key(la::VALUE)) {
                            result = lower_expr(map_of(s.get(la::VALUE.code)));
                        } else if (lc != la::EXPR_STMT && lc != la::LET && lc != la::LET_DESTRUCT && lc != la::RETURN) {
                            result = lower_expr(s);
                        } else {
                            block->stmts.push_back(lower_stmt(s));
                        }
                    } else {
                        block->stmts.push_back(lower_stmt(s));
                    }
                }
            }
            inside_unsafe_ = was;
            if (!result) return make_expr(void_t(), lir::EBlockExpr{std::move(block), nullptr});
            const LogosType* rt = result->type;
            return make_expr(rt, lir::EBlockExpr{std::move(block), std::move(result)});
        }

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
            // Allow pointer-vs-integer-literal comparison (null check: ptr == 0)
            bool ptr_null_cmp =
                (lt->kind == LogosType::Kind::Ptr && rt->kind == LogosType::Kind::IntLit) ||
                (rt->kind == LogosType::Kind::Ptr && lt->kind == LogosType::Kind::IntLit);
            bool ok = ptr_null_cmp || types_compatible(lt, rt) || types_compatible(rt, lt);
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
        } else if (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>") {
            // Bitwise and shift operators — require integer operands.
            if (!is_integer_kind(lt->kind) && lt->kind != LogosType::Kind::IntLit)
                error(std::format("operator '{}': left must be integer, got {}", op, type_str(lt)));
            if (!is_integer_kind(rt->kind) && rt->kind != LogosType::Kind::IntLit)
                error(std::format("operator '{}': right must be integer, got {}", op, type_str(rt)));
            result_type = unify_int(lt, rt);
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
                auto addr = make_expr(make_ref(false, vt->elem), lir::EAddrOf{std::string(var_name)});
                auto len  = make_expr(prim(LogosType::Kind::I64), lir::ELitInt{(int64_t)vt->arr_size});
                return make_expr(make_slice_type(vt->elem),
                    lir::ESliceLit{std::move(addr), std::move(len)});
            }
            return make_expr(make_ref(false, vt), lir::EAddrOf{std::string(var_name)});
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
        if (vt->kind != LogosType::Kind::Ptr &&
            vt->kind != LogosType::Kind::Ref &&
            vt->kind != LogosType::Kind::MutRef) {
            error(std::format("dereference of non-pointer type {}", type_str(vt)));
            return make_expr(error_t(), lir::EDeref{std::move(operand)});
        }
        // Raw pointer deref requires unsafe context
        if (vt->kind == LogosType::Kind::Ptr && !inside_unsafe_)
            error("dereference of raw pointer requires unsafe context");
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

        // format() is now a library function in std.fmt (variadic generics + Format trait).
        // The old intrinsic path (EFormatCall) is retained for future intrinsics but
        // no longer intercepts the "format" name.

        // Lower arguments first — needed for type inference
        std::vector<lir::LExprPtr> arg_exprs;
        if (node.has_key(la::ARGS)) {
            auto args = arr_of(node.get(la::ARGS.code));
            for (uint64_t i = 0; i < args.size(); ++i)
                arg_exprs.push_back(lower_expr(map_of(args.get(i))));
        }

        auto fit  = funcs_.find(std::string(callee));
        auto git  = generic_funcs_.find(std::string(callee));
        uint64_t n_args = arg_exprs.size();

        // Resolve the "best" SemaFuncInfo to try.
        // Priority:
        //   1. generic_funcs_ (variadic overload, or overloaded name) if callee is there
        //   2. funcs_ with non-empty type_params (plain generic fn, stored in funcs_)
        //   3. funcs_ with empty type_params (concrete fn)
        // If the function is generic (by either map or non-empty type_params in funcs_),
        // try to infer type args from the actual argument types.

        // Identify which entry to use
        const SemaFuncInfo* infer_fi = nullptr;
        if (fit == funcs_.end() && git == generic_funcs_.end()) {
            error(std::format("call to undefined function '{}'", callee));
            return make_expr(error_t(), lir::ECall{std::string(callee), {}, std::move(arg_exprs)});
        }
        // Pub check and unsafe check.
        {
            const SemaFuncInfo* fi_chk = (fit != funcs_.end()) ? &fit->second
                                       : &git->second;
            check_pub_access(fi_chk->is_pub, fi_chk->package, callee);
            if (fi_chk->is_unsafe && !inside_unsafe_)
                error(std::format("call to unsafe function '{}' requires unsafe context", callee));
        }

        // Determine if we should try inference
        bool try_inference = false;
        if (fit == funcs_.end()) {
            // Only in generic_funcs_
            infer_fi = &git->second;
            try_inference = true;
        } else if (!fit->second.type_params.empty()) {
            // In funcs_ but is a generic function (no non-generic overload exists)
            infer_fi = &fit->second;
            try_inference = true;
        } else if (git != generic_funcs_.end()) {
            // Non-generic in funcs_, generic overload in generic_funcs_.
            // Try generic when arity doesn't match the non-generic.
            bool arity_ok = fit->second.is_vararg
                ? n_args >= fit->second.param_types.size()
                : n_args == fit->second.param_types.size();
            if (!arity_ok) {
                infer_fi = &git->second;
                try_inference = true;
            }
        }

        if (try_inference && infer_fi) {
            // Don't infer inside generic bodies: pack expansions or TypeVar/AssocType args
            // indicate we're in a partially-substituted context — defer to mono.
            bool in_generic_context = false;
            for (auto& a : arg_exprs) {
                if (std::holds_alternative<lir::EPackExpand>(a->kind)) {
                    in_generic_context = true; break;
                }
                auto* t = a->type;
                if (t && (t->kind == LogosType::Kind::TypeVar ||
                          t->kind == LogosType::Kind::AssocType)) {
                    in_generic_context = true; break;
                }
            }
            if (!in_generic_context) {
                std::vector<const LogosType*> inferred;
                if (infer_type_args(*infer_fi, arg_exprs, inferred))
                    return finish_generic_call(callee, *infer_fi, std::move(inferred), std::move(arg_exprs));
                error(std::format("call to '{}': could not infer all type arguments — use explicit f::<T>(...) syntax", callee));
                return make_expr(error_t(), lir::ECall{std::string(callee), {}, std::move(arg_exprs)});
            }
            // Fall through to non-generic path (mono will handle instantiation)
        }

        // Non-generic path
        auto& fi = fit->second;

        // If any arg is a pack expansion, skip checking — mono will expand
        bool has_pack_expand = false;
        for (auto& a : arg_exprs)
            if (std::holds_alternative<lir::EPackExpand>(a->kind))
                has_pack_expand = true;

        if (has_pack_expand) {
            // Pass through — mono will expand and validate
        } else if (fi.is_vararg) {
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

        // Move semantics: mark by-value move-type args as moved
        for (auto& a : arg_exprs) {
            if (is_move_type(a->type)) {
                if (auto* vr = std::get_if<lir::EVarRef>(&a->kind))
                    mark_moved(vr->name);
            }
        }

        return make_expr(fi.ret_type, lir::ECall{std::string(callee), {}, std::move(arg_exprs)});
    }

    // ── Type inference helpers ────────────────────────────────────

    // Recursively unify a formal parameter type (may contain TypeVars) against
    // a concrete actual type, populating bindings.  On conflict keeps the first
    // binding (first-wins semantics; callers may validate consistency later).
    void unify_types(const LogosType* formal, const LogosType* actual,
                     std::unordered_map<std::string, const LogosType*>& bindings) {
        if (!formal || !actual) return;
        if (actual->kind == LogosType::Kind::Error ||
            formal->kind == LogosType::Kind::Error) return;

        // Widen IntLit to i32 before any binding
        const LogosType* actual_norm = actual;
        if (actual->kind == LogosType::Kind::IntLit)
            actual_norm = prim(LogosType::Kind::I32);

        if (formal->kind == LogosType::Kind::TypeVar) {
            if (formal->type_var_name == "Self") return;  // skip implicit Self
            if (!bindings.count(formal->type_var_name))
                bindings[formal->type_var_name] = actual_norm;
            return;
        }

        switch (formal->kind) {
        case LogosType::Kind::Ptr:
            if (actual_norm->kind == LogosType::Kind::Ptr)
                unify_types(formal->pointee, actual_norm->pointee, bindings);
            else if (actual_norm->kind == LogosType::Kind::Ref ||
                     actual_norm->kind == LogosType::Kind::MutRef)
                unify_types(formal->pointee, actual_norm->pointee, bindings);
            break;
        case LogosType::Kind::Ref:
        case LogosType::Kind::MutRef:
            if (actual_norm->kind == LogosType::Kind::Ref ||
                actual_norm->kind == LogosType::Kind::MutRef ||
                actual_norm->kind == LogosType::Kind::Ptr)
                unify_types(formal->pointee, actual_norm->pointee, bindings);
            break;
        case LogosType::Kind::Array:
            if (actual_norm->kind == LogosType::Kind::Array)
                unify_types(formal->elem, actual_norm->elem, bindings);
            break;
        case LogosType::Kind::Slice:
            if (actual_norm->kind == LogosType::Kind::Slice)
                unify_types(formal->elem, actual_norm->elem, bindings);
            break;
        case LogosType::Kind::Struct:
            if (actual_norm->kind == LogosType::Kind::Struct &&
                formal->struct_name == actual_norm->struct_name) {
                for (size_t i = 0; i < formal->type_args.size() &&
                                    i < actual_norm->type_args.size(); ++i)
                    unify_types(formal->type_args[i], actual_norm->type_args[i], bindings);
            }
            break;
        case LogosType::Kind::Tuple:
            if (actual_norm->kind == LogosType::Kind::Tuple) {
                for (size_t i = 0; i < formal->tuple_elems.size() &&
                                    i < actual_norm->tuple_elems.size(); ++i)
                    unify_types(formal->tuple_elems[i], actual_norm->tuple_elems[i], bindings);
            }
            break;
        default:
            break;  // concrete type — nothing to bind
        }
    }

    // Try to infer type_args for a generic call from the actual argument types.
    // Returns true when every required type param (non-variadic) was resolved.
    // For variadic packs, each extra arg contributes one pack element type.
    bool infer_type_args(const SemaFuncInfo& fi,
                         const std::vector<lir::LExprPtr>& arg_exprs,
                         std::vector<const LogosType*>& out_type_args,
                         const SemaSubst& context = {},
                         size_t param_offset = 0) {
        std::unordered_map<std::string, const LogosType*> bindings(context.begin(), context.end());
        bool has_variadic = !fi.type_params.empty() && fi.type_params.back().is_variadic;
        size_t non_variadic_count = fi.type_params.size() - (has_variadic ? 1 : 0);
        size_t fixed_params = fi.param_types.size() >= param_offset
            ? fi.param_types.size() - param_offset - (has_variadic ? 1 : 0)
            : 0;

        // Unify fixed params against arg types
        for (size_t i = 0; i < fixed_params && i < arg_exprs.size(); ++i) {
            auto* pt = fi.param_types[param_offset + i];
            if (!context.empty()) pt = subst_type_sema(pt, context);
            unify_types(pt, arg_exprs[i]->type, bindings);
        }

        // Build type_args: non-variadic params first
        out_type_args.clear();
        for (size_t i = 0; i < non_variadic_count; ++i) {
            auto it = bindings.find(fi.type_params[i].name);
            if (it == bindings.end()) return false;  // param not inferrable
            out_type_args.push_back(it->second);
        }

        // Variadic pack: each arg beyond fixed_params contributes one pack element
        if (has_variadic) {
            for (size_t i = fixed_params; i < arg_exprs.size(); ++i) {
                auto* t = arg_exprs[i]->type;
                if (t->kind == LogosType::Kind::IntLit)
                    t = prim(LogosType::Kind::I32);
                out_type_args.push_back(t);
            }
        }
        return true;
    }

    // ── Shared generic call validation + emission ─────────────────
    // Called from lower_generic_call (explicit types) and from lower_call
    // (inferred types).  Validates bounds, arity, arg types, computes ret.

    lir::LExprPtr finish_generic_call(std::string_view callee_sv,
                                       const SemaFuncInfo& fi,
                                       std::vector<const LogosType*> type_args,
                                       std::vector<lir::LExprPtr> arg_exprs) {
        std::string callee{callee_sv};
        // Unsafe check: covers both inferred (lower_call) and explicit (lower_generic_call) paths.
        if (fi.is_unsafe && !inside_unsafe_)
            error(std::format("call to unsafe function '{}' requires unsafe context", callee_sv));
        bool has_variadic = !fi.type_params.empty() && fi.type_params.back().is_variadic;
        size_t non_variadic_count = fi.type_params.size() - (has_variadic ? 1 : 0);

        // Validate type arg count
        if (!fi.type_params.empty()) {
            if (has_variadic) {
                if (type_args.size() < non_variadic_count)
                    error(std::format("call to '{}': expected at least {} type arg(s), got {}",
                          callee, non_variadic_count, type_args.size()));
            } else if (type_args.size() != fi.type_params.size()) {
                error(std::format("call to '{}': expected {} type arg(s), got {}",
                      callee, fi.type_params.size(), type_args.size()));
            }
        }

        // Build substitution map for non-variadic type params
        std::unordered_map<std::string, const LogosType*> subst;
        for (size_t i = 0; i < non_variadic_count && i < type_args.size(); ++i)
            subst[fi.type_params[i].name] = type_args[i];

        // Validate trait bounds for all type params (including variadic pack elements)
        check_type_bounds(std::string(callee), fi.type_params, type_args);

        // Substitute return type
        const LogosType* ret = subst_type_sema(fi.ret_type, subst);

        // Validate value argument count and types
        uint64_t n_args = arg_exprs.size();
        bool has_pack_expand = false;
        for (auto& a : arg_exprs)
            if (std::holds_alternative<lir::EPackExpand>(a->kind)) {
                has_pack_expand = true; break;
            }

        if (has_pack_expand) {
            // pass — mono expands
        } else if (has_variadic) {
            size_t fixed_params = fi.param_types.size() - 1;
            if (n_args < fixed_params)
                error(std::format("call to '{}': expected at least {} args, got {}",
                      callee, fixed_params, n_args));
            for (uint64_t i = 0; i < fixed_params && i < n_args; ++i) {
                auto* at = arg_exprs[i]->type;
                auto* pt = subst_type_sema(fi.param_types[i], subst);
                if (at->kind != LogosType::Kind::Error &&
                    pt->kind != LogosType::Kind::Error &&
                    pt->kind != LogosType::Kind::TypeVar &&
                    !types_compatible(at, pt))
                    error(std::format("call to '{}' arg {}: expected {}, got {}",
                          callee, i + 1, type_str(pt), type_str(at)));
            }
        } else {
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
                        pt->kind != LogosType::Kind::AssocType &&
                        !types_compatible(at, pt))
                        error(std::format("call to '{}' arg {}: expected {}, got {}",
                              callee, i + 1, type_str(pt), type_str(at)));
                }
            }
        }

        return make_expr(ret, lir::ECall{callee, std::move(type_args), std::move(arg_exprs)});
    }

    // ── lower_generic_call: foo::<T1, T2>(args) ──────────────────

    lir::LExprPtr lower_generic_call(TinyMapView node) {
        auto callee = str_of(node.get(la::CALLEE.code));

        // sizeof::<T>() — compiler builtin, returns i64 byte size of T.
        if (callee == "sizeof") {
            const LogosType* elem = nullptr;
            if (node.has_key(la::TYPE_PARAMS)) {
                auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
                if (tplist.has_key(la::ITEMS)) {
                    auto items = arr_of(tplist.get(la::ITEMS.code));
                    if (items.size() == 1)
                        elem = resolve_type(map_of(items.get(0)));
                }
            }
            if (!elem) error("sizeof::<T>() requires exactly one type argument");
            return make_expr(prim(LogosType::Kind::I64), lir::ESizeOf{elem});
        }

        // Prefer the generic overload (for variadic base case overloading)
        SemaFuncInfo* fi_ptr = nullptr;
        {
            auto git = generic_funcs_.find(std::string(callee));
            if (git != generic_funcs_.end()) fi_ptr = &git->second;
            else {
                auto fit2 = funcs_.find(std::string(callee));
                if (fit2 != funcs_.end()) fi_ptr = &fit2->second;
            }
        }
        if (!fi_ptr) {
            error(std::format("call to undefined function '{}'", callee));
            return make_expr(error_t(), lir::ECall{std::string(callee), {}, {}});
        }
        check_pub_access(fi_ptr->is_pub, fi_ptr->package, callee);

        // Resolve explicit type arguments from TYPE_PARAMS
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

        // Resolve value arguments
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

        return finish_generic_call(callee, *fi_ptr, std::move(type_args), std::move(arg_exprs));
    }

    lir::LExprPtr lower_class_method_call(lir::LExprPtr recv,
                                           std::string_view cname,
                                           std::string_view method_name,
                                           TinyMapView node) {
        // Walk inheritance chain to find the method.
        std::string resolved_class;
        std::string mangled;
        SemaSubst recv_subst;
        {
            std::string start_class = std::string(cname);
            std::string cur = start_class;
            // Build subst: start_class type params → receiver's concrete type args
            {
                const LogosType* recv_t = recv->type;
                if (recv_t && is_ref_like(recv_t->kind) && recv_t->pointee)
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
                if (funcs_.count(candidate) || generic_funcs_.count(candidate)) {
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

        const SemaFuncInfo* fi_ptr = nullptr;
        if (auto fit = funcs_.find(mangled); fit != funcs_.end()) fi_ptr = &fit->second;
        else if (auto git = generic_funcs_.find(mangled); git != generic_funcs_.end()) fi_ptr = &git->second;

        if (!fi_ptr) {
            error(std::format("class '{}' has no method '{}'", cname, method_name));
            std::vector<lir::LExprPtr> dummy_args;
            return make_expr(error_t(),
                lir::EMethodCall{std::move(recv), std::string(method_name), {}, std::move(dummy_args), -1, ""});
        }

        std::vector<lir::LExprPtr> arg_exprs;
        if (node.has_key(la::ARGS)) {
            auto args = arr_of(node.get(la::ARGS.code));
            for (uint64_t i = 0; i < args.size(); ++i)
                arg_exprs.push_back(lower_expr(map_of(args.get(i))));
        }

        auto& fi = *fi_ptr;
        check_pub_access(fi.is_pub, fi.package, mangled);
        if (fi.is_unsafe && !inside_unsafe_)
            error(std::format("call to unsafe method '{}' requires unsafe context", mangled));

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
                    auto* pt = recv_subst.empty() ? fi.param_types[pi]
                                                  : subst_type_sema(fi.param_types[pi], recv_subst);
                    if (at->kind != LogosType::Kind::Error && pt->kind != LogosType::Kind::Error &&
                        !compat(at, pt))
                        error(std::format("method '{}' arg {}: expected {}, got {}",
                              mangled, i + 1, type_str(pt), type_str(at)));
                }
            }
        }

        const LogosType* ret_t = recv_subst.empty() ? fi.ret_type
                                                      : subst_type_sema(fi.ret_type, recv_subst);

        int32_t vidx = vtable_index_of(cname, mangled);

        // Infer method type args if generic (Bug 12)
        std::vector<const LogosType*> m_type_args;
        if (!fi.type_params.empty()) {
            if (!infer_type_args(fi, arg_exprs, m_type_args, recv_subst, 1)) {
                error(std::format("could not infer type arguments for generic method '{}'", mangled));
            }
            check_type_bounds(mangled, fi.type_params, m_type_args);
            // Re-substitute return type with BOTH struct and method bindings
            SemaSubst combined = recv_subst;
            for (size_t i = 0; i < fi.type_params.size() && i < m_type_args.size(); ++i)
                combined[fi.type_params[i].name] = m_type_args[i];
            ret_t = subst_type_sema(fi.ret_type, combined);
        }

        lir::EMethodCall mc;
        mc.receiver      = std::move(recv);
        mc.method        = std::string(method_name);
        mc.type_args     = std::move(m_type_args);
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

        // &dyn Trait method call: look up trait method, emit EMethodCall with vtable dispatch.
        if (recv->type->kind == LogosType::Kind::TraitObject) {
            auto& tname = recv->type->trait_name;
            auto tit = traits_.find(tname);
            if (tit != traits_.end()) {
                for (size_t mi = 0; mi < tit->second.methods.size(); ++mi) {
                    auto& m = tit->second.methods[mi];
                    if (m.name == method_name) {
                        if (m.is_unsafe && !inside_unsafe_)
                            error(std::format("call to unsafe method '{}' requires unsafe context",
                                              std::string(method_name)));
                        std::vector<lir::LExprPtr> arg_exprs;
                        if (node.has_key(la::ARGS)) {
                            auto args = arr_of(node.get(la::ARGS.code));
                            for (uint64_t i = 0; i < args.size(); ++i)
                                arg_exprs.push_back(lower_expr(map_of(args.get(i))));
                        }
                        uint64_t explicit_args = arg_exprs.size();
                        size_t expected_explicit = m.param_types.size() > 0
                            ? m.param_types.size() - 1 : 0;
                        if (explicit_args != expected_explicit) {
                            error(std::format("method call '{}': expected {} args, got {}",
                                              std::string(method_name), expected_explicit, explicit_args));
                        } else {
                            SemaSubst self_subst;
                            self_subst["Self"] = recv->type;
                            for (uint64_t i = 0; i < explicit_args; ++i) {
                                auto* at = arg_exprs[i]->type;
                                auto* pt = subst_type_sema(m.param_types[i + 1], self_subst);
                                if (at->kind != LogosType::Kind::Error &&
                                    pt->kind != LogosType::Kind::Error &&
                                    pt->kind != LogosType::Kind::TypeVar &&
                                    pt->kind != LogosType::Kind::AssocType &&
                                    !types_compatible(at, pt))
                                    error(std::format("method '{}' arg {}: expected {}, got {}",
                                                      std::string(method_name), i + 1,
                                                      type_str(pt), type_str(at)));
                            }
                        }
                        // Return type: substitute Self → &dyn Trait
                        auto* ret_type = m.ret_type;
                        if (ret_type && ret_type->kind == LogosType::Kind::TypeVar &&
                            ret_type->type_var_name == "Self")
                            ret_type = recv->type;
                        lir::EMethodCall mc;
                        mc.receiver     = std::move(recv);
                        mc.method       = std::string(method_name);
                        mc.type_args    = {}; // No type args for trait object calls for now
                        mc.args         = std::move(arg_exprs);
                        mc.vtable_index = (int32_t)mi;  // slot in vtable
                        mc.resolved_type = "";
                        return make_expr(ret_type, std::move(mc));
                    }
                }
            }
            error(std::format("trait '{}' has no method '{}'", tname, method_name));
            return error_expr();
        }

        // TypeVar with trait bounds: look up trait method signature.
        // The actual impl method will be resolved during monomorphization.
        // Handle both T and *mut T / *const T / &T / &mut T receivers.
        const LogosType* recv_inner = recv->type;
        if (recv_inner && recv_inner->kind == LogosType::Kind::Ptr) {
            if (!inside_unsafe_)
                error("method call through raw pointer requires unsafe context");
            recv_inner = recv_inner->pointee;
        } else if (recv_inner && is_ref_like(recv_inner->kind) && recv_inner->pointee) {
            recv_inner = recv_inner->pointee;
        }
        if (recv_inner->kind == LogosType::Kind::TypeVar) {
            std::vector<lir::LExprPtr> arg_exprs;
            if (node.has_key(la::ARGS)) {
                auto args = arr_of(node.get(la::ARGS.code));
                for (uint64_t i = 0; i < args.size(); ++i)
                    arg_exprs.push_back(lower_expr(map_of(args.get(i))));
            }

            auto bit = current_type_bounds_.find(recv_inner->type_var_name);
            const SemaTraitMethodInfo* chosen_method = nullptr;
            std::string chosen_trait;
            if (bit != current_type_bounds_.end()) {
                for (auto& bound : bit->second) {
                auto tit = traits_.find(bound.trait_name);
                if (tit == traits_.end()) continue;
                for (auto& m : tit->second.methods) {
                    if (m.name != method_name) continue;
                    if (chosen_method && chosen_trait != bound.trait_name) {
                        error(std::format(
                            "method '{}' is ambiguous for type parameter '{}' (matches traits '{}' and '{}')",
                            std::string(method_name), recv_inner->type_var_name, chosen_trait, bound.trait_name));
                    }
                    chosen_method = &m;
                    chosen_trait = bound.trait_name;
                }
            }
            }

            if (chosen_method) {
                if (chosen_method->is_unsafe && !inside_unsafe_)
                    error(std::format("call to unsafe method '{}' requires unsafe context",
                                      std::string(method_name)));

                size_t expected_explicit = chosen_method->param_types.size() > 0
                    ? chosen_method->param_types.size() - 1 : 0;
                if (arg_exprs.size() != expected_explicit) {
                    error(std::format("method call '{}': expected {} args, got {}",
                                      std::string(method_name), expected_explicit, arg_exprs.size()));
                } else {
                    SemaSubst self_subst;
                    self_subst["Self"] = recv_inner;
                    for (uint64_t i = 0; i < arg_exprs.size(); ++i) {
                        auto* at = arg_exprs[i]->type;
                        auto* pt = subst_type_sema(chosen_method->param_types[i + 1], self_subst);
                        if (at->kind != LogosType::Kind::Error &&
                            pt->kind != LogosType::Kind::Error &&
                            pt->kind != LogosType::Kind::TypeVar &&
                            pt->kind != LogosType::Kind::AssocType &&
                            !types_compatible(at, pt))
                            error(std::format("method '{}' arg {}: expected {}, got {}",
                                              std::string(method_name), i + 1,
                                              type_str(pt), type_str(at)));
                    }
                }

                SemaSubst self_subst;
                self_subst["Self"] = recv_inner;
                const LogosType* ret_type = subst_type_sema(chosen_method->ret_type, self_subst);

                // Use EMethodCall — mono will resolve to concrete impl.
                lir::EMethodCall mc;
                mc.receiver = std::move(recv);
                mc.method   = std::string(method_name);
                mc.type_args = {};
                mc.args     = std::move(arg_exprs);
                mc.vtable_index = -1;
                mc.resolved_type = "";
                return make_expr(ret_type, std::move(mc));
            }

            error(std::format("type parameter '{}' has no trait bound providing method '{}'",
                              recv_inner->type_var_name, std::string(method_name)));
            return make_expr(error_t(),
                lir::EMethodCall{std::move(recv), std::string(method_name), {}, std::move(arg_exprs), -1, ""});
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

        // For primitive types, non-generic enums, etc.: try TypeName__method
        if (sname.empty()) {
            // For generic enums (Enum<T> with type_args): instantiate method with concrete types.
            // type_str returns just the base name (e.g. "Option" for Option<i32>), so we must
            // handle this BEFORE the generic lookup to avoid calling the uninstantiated template.
            if (recv->type->kind == LogosType::Kind::Enum &&
                !recv->type->type_args.empty()) {
                const std::string& base = recv->type->enum_name;
                auto generic_key = base + "__" + std::string(method_name);
                const SemaFuncInfo* fi_ptr = nullptr;
                if (auto git = funcs_.find(generic_key); git != funcs_.end()) fi_ptr = &git->second;
                else if (auto git = generic_funcs_.find(generic_key); git != generic_funcs_.end()) fi_ptr = &git->second;

                if (fi_ptr && !fi_ptr->type_params.empty()) {
                    if (fi_ptr->is_unsafe && !inside_unsafe_)
                        error(std::format("call to unsafe method '{}' requires unsafe context",
                                          generic_key));
                    // Build concrete name from enum base + type args + method
                    // e.g. "Option__i32__unwrap_or"
                    SemaSubst subst;
                    auto eit = enums_.find(base);
                    if (eit != enums_.end()) {
                        auto& tps = eit->second.type_params;
                        for (size_t i = 0; i < tps.size() && i < recv->type->type_args.size(); ++i)
                            subst[tps[i].name] = recv->type->type_args[i];
                    }
                    const LogosType* ret = subst_type_sema(fi_ptr->ret_type, subst);
                    // Mangle: "Option__i32" is the concrete enum name
                    std::string concrete_enum = base;
                    for (auto* ta : recv->type->type_args) {
                        concrete_enum += "__";
                        concrete_enum += type_str(ta);
                    }
                    std::string concrete_mangled = concrete_enum + "__" + std::string(method_name);
                    std::vector<lir::LExprPtr> pargs;
                    pargs.push_back(std::move(recv));
                    for (auto& a : arg_exprs) pargs.push_back(std::move(a));
                    return make_expr(ret, lir::ECall{concrete_mangled, {}, std::move(pargs)});
                }
            }
            auto tname = type_str(recv->type);
            auto mangled_prim = tname + "__" + std::string(method_name);
            const SemaFuncInfo* fi_ptr = nullptr;
            if (auto pfit = funcs_.find(mangled_prim); pfit != funcs_.end()) fi_ptr = &pfit->second;
            else if (auto pfit = generic_funcs_.find(mangled_prim); pfit != generic_funcs_.end()) fi_ptr = &pfit->second;

            if (fi_ptr) {
                if (fi_ptr->is_unsafe && !inside_unsafe_)
                    error(std::format("call to unsafe method '{}' requires unsafe context", mangled_prim));
                std::vector<lir::LExprPtr> pargs;
                pargs.push_back(std::move(recv));
                for (auto& a : arg_exprs) pargs.push_back(std::move(a));
                return make_expr(fi_ptr->ret_type,
                    lir::ECall{mangled_prim, {}, std::move(pargs)});
            }
            error(std::format("method call: receiver is not a struct (got {})",
                  type_str(recv->type)));
            return make_expr(error_t(),
                lir::EMethodCall{std::move(recv), std::string(method_name), {}, std::move(arg_exprs), -1, ""});
        }

        auto mangled = std::string(sname) + "__" + std::string(method_name);
        const SemaFuncInfo* fi_ptr = nullptr;
        if (auto fit = funcs_.find(mangled); fit != funcs_.end()) fi_ptr = &fit->second;
        else if (auto fit = generic_funcs_.find(mangled); fit != generic_funcs_.end()) fi_ptr = &fit->second;

        if (!fi_ptr) {
            error(std::format("method call: '{}' has no method '{}'", sname, method_name));
            return make_expr(error_t(),
                lir::EMethodCall{std::move(recv), std::string(method_name), {}, std::move(arg_exprs), -1, ""});
        }

        auto& fi = *fi_ptr;
        check_pub_access(fi.is_pub, fi.package, mangled);
        if (fi.is_unsafe && !inside_unsafe_)
            error(std::format("call to unsafe method '{}' requires unsafe context", mangled));

        // Build TypeVar→concrete substitution from the receiver's struct type args.
        // This lets us check e.g. Vec<i32>::push(val: T) with T resolved to i32.
        SemaSubst struct_subst;
        {
            const LogosType* rst = recv->type;
            if (rst && rst->kind == LogosType::Kind::Ptr) {
                if (!inside_unsafe_)
                    error("method call through raw pointer requires unsafe context");
                if (rst->pointee) rst = rst->pointee;
            } else if (rst && is_ref_like(rst->kind) && rst->pointee) {
                rst = rst->pointee;
            }
            if (rst->kind == LogosType::Kind::Struct && !rst->type_args.empty()) {
                auto sit2 = structs_.find(rst->struct_name);
                if (sit2 != structs_.end()) {
                    auto& tps = sit2->second.type_params;
                    for (size_t i = 0; i < tps.size() && i < rst->type_args.size(); ++i)
                        struct_subst[tps[i].name] = rst->type_args[i];
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
                    auto* pt = fi.param_types[pi];
                    if (!struct_subst.empty()) pt = subst_type_sema(pt, struct_subst);
                    if (at->kind != LogosType::Kind::Error && pt->kind != LogosType::Kind::Error &&
                        !types_compatible(at, pt))
                        error(std::format("method '{}' arg {}: expected {}, got {}",
                              mangled, i + 1, type_str(pt), type_str(at)));
                }
            }
        }

        // Infer method type args if generic (Bug 12)
        std::vector<const LogosType*> m_type_args;
        if (!fi.type_params.empty()) {
            if (!infer_type_args(fi, arg_exprs, m_type_args, struct_subst, 1)) {
                error(std::format("could not infer type arguments for generic method '{}'", mangled));
            }
            check_type_bounds(mangled, fi.type_params, m_type_args);
            // Merge method bindings into struct_subst for return type substitution
            for (size_t i = 0; i < fi.type_params.size() && i < m_type_args.size(); ++i)
                struct_subst[fi.type_params[i].name] = m_type_args[i];
        }

        // Substitute TypeVars in return type using the combined substitution.
        const LogosType* ret = struct_subst.empty()
            ? fi.ret_type : subst_type_sema(fi.ret_type, struct_subst);

        lir::EMethodCall mc;
        mc.receiver     = std::move(recv);
        mc.method       = std::string(method_name);
        mc.type_args    = std::move(m_type_args);
        mc.args         = std::move(arg_exprs);
        mc.vtable_index = -1;
        mc.resolved_type = "";
        return make_expr(ret, std::move(mc));
    }

    lir::LExprPtr lower_field_read(TinyMapView node) {
        auto field_name = str_of(node.get(la::FIELD.code));
        auto recv = lower_expr(map_of(node.get(la::RECEIVER.code)));
        const LogosType* recv_base_t = recv->type;
        if (recv_base_t && recv_base_t->kind == LogosType::Kind::Ptr) {
            if (!inside_unsafe_)
                error("field read through raw pointer requires unsafe context");
            recv_base_t = recv_base_t->pointee;
        } else if (recv_base_t && is_ref_like(recv_base_t->kind)) {
            recv_base_t = recv_base_t->pointee;
        }

        // Check for class receiver
        auto cname_sv = class_name_from_type(recv_base_t);
        if (!cname_sv.empty()) {
            auto* ft = class_field_type(cname_sv, field_name);
            if (!ft) {
                error(std::format("field read: class '{}' has no field '{}'", cname_sv, field_name));
                return make_expr(error_t(), lir::EFieldRead{std::move(recv), std::string(field_name)});
            }
            // Pub check for class field reads.
            {
                auto cit = classes_.find(std::string(cname_sv));
                if (cit != classes_.end()) {
                    for (auto& f : cit->second.all_fields) {
                        if (f.name == field_name) {
                            check_pub_access(f.is_pub, cit->second.package, field_name);
                            break;
                        }
                    }
                }
            }
            return make_expr(ft, lir::EFieldRead{std::move(recv), std::string(field_name)});
        }

        auto sname = struct_name_from_type(recv_base_t);
        if (sname.empty()) {
            error(std::format("field read: receiver is not a struct or class (got {})",
                  type_str(recv->type)));
            return make_expr(error_t(), lir::EFieldRead{std::move(recv), std::string(field_name)});
        }
        // Resolve the actual struct type (receiver may be a pointer/reference to a struct).
        const LogosType* recv_struct_t = recv_base_t;
        auto* ft = field_type_of_for_type(recv_struct_t, field_name);
        if (!ft) {
            error(std::format("field read: struct '{}' has no field '{}'", sname, field_name));
            return make_expr(error_t(), lir::EFieldRead{std::move(recv), std::string(field_name)});
        }
        // Pub check: private fields are accessible only within the defining package.
        {
            auto sit = structs_.find(std::string(sname));
            if (sit != structs_.end()) {
                for (auto& f : sit->second.fields) {
                    if (f.name == field_name || (f.is_variadic && field_name.starts_with(f.name) && field_name.size() > f.name.size() + 1 && field_name[f.name.size()] == '_')) {
                        check_pub_access(f.is_pub, sit->second.package, field_name);
                        break;
                    }
                }
            }
            // Specs: check against their own SemaStructInfo (which also stores package).
            else {
                auto spec_it = struct_specs_sema_.find(std::string(sname));
                if (spec_it != struct_specs_sema_.end()) {
                    for (auto& f : spec_it->second.fields) {
                        if (f.name == field_name || (f.is_variadic && field_name.starts_with(f.name) && field_name.size() > f.name.size() + 1 && field_name[f.name.size()] == '_')) {
                            check_pub_access(f.is_pub, spec_it->second.package, field_name);
                            break;
                        }
                    }
                }
            }
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

        // Pub check: struct with private fields can only be constructed within its package.
        if (sinfo.package != cur_package_ && !sinfo.package.empty() && !cur_package_.empty()) {
            for (auto& f : sinfo.fields) {
                if (!f.is_pub) {
                    error(std::format("cannot construct '{}' from package '{}': field '{}' is private",
                          sname, cur_package_, f.name));
                    break;
                }
            }
        }

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
            // Helper: get the hint type for a TypeVar from hint_struct_type_ annotation.
            auto hint_for_tv = [&](const std::string& tv_name) -> const LogosType* {
                if (!hint_struct_type_ || hint_struct_type_->struct_name != std::string(sname))
                    return nullptr;
                for (size_t i = 0; i < sinfo.type_params.size(); ++i)
                    if (sinfo.type_params[i].name == tv_name &&
                        i < hint_struct_type_->type_args.size())
                        return hint_struct_type_->type_args[i];
                return nullptr;
            };

            // Infer type args from field values against the generic template.
            SemaSubst inferred;
            for (auto& [fname, fval] : fields) {
                auto* raw_ft = field_type_of(std::string(sname), fname);
                if (!raw_ft) continue;
                if (raw_ft->kind == LogosType::Kind::TypeVar) {
                    auto& tv = raw_ft->type_var_name;
                    if (!inferred.count(tv)) {
                        auto* vt = fval->type;
                        if (vt->kind == LogosType::Kind::IntLit) {
                            auto* h = hint_for_tv(tv);
                            vt = (h && h->kind != LogosType::Kind::Error) ? h : i32_t();
                        }
                        inferred[tv] = vt;
                    }
                } else if (raw_ft->kind == LogosType::Kind::Array && raw_ft->elem &&
                           raw_ft->elem->kind == LogosType::Kind::TypeVar) {
                    // [T; N] field — infer T from element type of the value.
                    auto& tv = raw_ft->elem->type_var_name;
                    if (!inferred.count(tv) && fval->type->kind == LogosType::Kind::Array &&
                        fval->type->elem) {
                        auto* vt = fval->type->elem;
                        if (vt->kind == LogosType::Kind::IntLit) {
                            auto* h = hint_for_tv(tv);
                            vt = (h && h->kind != LogosType::Kind::Error) ? h : i32_t();
                        }
                        inferred[tv] = vt;
                    }
                } else if ((raw_ft->kind == LogosType::Kind::Ptr ||
                            raw_ft->kind == LogosType::Kind::Ref ||
                            raw_ft->kind == LogosType::Kind::MutRef) && raw_ft->pointee &&
                           raw_ft->pointee->kind == LogosType::Kind::TypeVar) {
                    // *T / &T / &mut T field — infer T from the value's pointee type.
                    auto& tv = raw_ft->pointee->type_var_name;
                    if (!inferred.count(tv) && is_ref_like(fval->type->kind) &&
                        fval->type->pointee) {
                        auto* vt = fval->type->pointee;
                        if (vt->kind != LogosType::Kind::Error)
                            inferred[tv] = vt;
                    }
                }
            }
            // For any TypeVar still not inferred from fields, fall back to hint.
            for (auto& tp : sinfo.type_params) {
                if (!inferred.count(tp.name)) {
                    auto* h = hint_for_tv(tp.name);
                    if (h && h->kind != LogosType::Kind::Error)
                        inferred[tp.name] = h;
                }
            }
            std::vector<const LogosType*> args;
            for (size_t i = 0, h_idx = 0; i < sinfo.type_params.size(); ++i) {
                auto& tp = sinfo.type_params[i];
                if (tp.is_variadic) {
                    if (hint_struct_type_ && hint_struct_type_->struct_name == std::string(sname)) {
                        while (h_idx < hint_struct_type_->type_args.size())
                            args.push_back(hint_struct_type_->type_args[h_idx++]);
                    } else {
                        auto it = inferred.find(tp.name);
                        args.push_back(it != inferred.end() ? it->second : error_t());
                    }
                    break;
                } else {
                    auto it = inferred.find(tp.name);
                    if (it != inferred.end()) {
                        args.push_back(it->second);
                        h_idx++;
                    } else if (hint_struct_type_ && hint_struct_type_->struct_name == std::string(sname) && h_idx < hint_struct_type_->type_args.size()) {
                        args.push_back(hint_struct_type_->type_args[h_idx++]);
                    } else {
                        args.push_back(error_t());
                    }
                }
            }
            check_type_bounds(std::string(sname), sinfo.type_params, args);
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
                    // Check if this matches a variadic field expansion
                    bool matched_variadic = false;
                    for (auto& f : effective->fields) {
                        if (f.is_variadic && fname.starts_with(f.name) && fname.size() > f.name.size() + 1 && fname[f.name.size()] == '_') {
                            initialized[std::string(f.name)] = true;
                            matched_variadic = true;
                            // Type check against the variadic field's type
                            if (f.type && f.type->kind != LogosType::Kind::Error &&
                                fval->type->kind != LogosType::Kind::Error &&
                                f.type->kind != LogosType::Kind::TypeVar &&
                                !types_compatible(fval->type, f.type)) {
                                error(std::format("struct literal '{}' field '{}': expected {}, got {}",
                                      sname, fname, type_str(f.type), type_str(fval->type)));
                            }
                            break;
                        }
                    }
                    if (!matched_variadic)
                        error(std::format("struct literal '{}': unknown field '{}'", sname, fname));
                } else {
                    it->second = true;
                    // Find field type in effective definition.
                    const LogosType* ft = nullptr;
                    for (auto& ef : effective->fields)
                        if (ef.name == fname) { ft = ef.type; break; }
                    bool ft_has_typevar = ft && (ft->kind == LogosType::Kind::TypeVar ||
                        (ft->kind == LogosType::Kind::Array && ft->elem &&
                         ft->elem->kind == LogosType::Kind::TypeVar));
                    if (ft && ft->kind != LogosType::Kind::Error &&
                        fval->type->kind != LogosType::Kind::Error &&
                        !ft_has_typevar &&
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
                // Check variadic
                bool matched_variadic = false;
                for (auto& f : sinfo.fields) {
                    if (f.is_variadic && fname.starts_with(f.name) && fname.size() > f.name.size() + 1 && fname[f.name.size()] == '_') {
                        initialized[std::string(f.name)] = true;
                        matched_variadic = true;
                        auto* ft = f.type;
                        if (ft && ft->kind != LogosType::Kind::Error &&
                            fval->type->kind != LogosType::Kind::Error &&
                            !types_compatible(fval->type, ft)) {
                            error(std::format("struct literal '{}' field '{}': expected {}, got {}",
                                  sname, fname, type_str(ft), type_str(fval->type)));
                        }
                        break;
                    }
                }
                if (!matched_variadic)
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

        // Move semantics: mark Move-typed field values as consumed.
        for (auto& [fname, fval] : fields) {
            if (fval && is_move_type(fval->type))
                if (auto* vr = std::get_if<lir::EVarRef>(&fval->kind))
                    mark_moved(vr->name);
        }

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
            arr_type->kind != LogosType::Kind::Ref &&
            arr_type->kind != LogosType::Kind::MutRef &&
            arr_type->kind != LogosType::Kind::Error) {
            error(std::format("index read: receiver is not an array, slice, or pointer (got {})",
                  type_str(arr_type)));
        }
        if (arr_type->kind == LogosType::Kind::Ptr && !inside_unsafe_) {
            error("index read through raw pointer requires unsafe context");
        }

        const LogosType* elem = error_t();
        if (arr_type->kind == LogosType::Kind::Array && arr_type->elem)  elem = arr_type->elem;
        if ((arr_type->kind == LogosType::Kind::Ptr ||
             arr_type->kind == LogosType::Kind::Ref ||
             arr_type->kind == LogosType::Kind::MutRef) && arr_type->pointee)
            elem = arr_type->pointee;

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

    lir::LExprPtr lower_arr_fill_lit(TinyMapView node) {
        auto val_node = map_of(node.get(la::VALUE.code));
        auto fill_val = lower_expr(val_node);
        auto sv = str_of(node.get(la::SIZE.code));
        int64_t n = (int64_t)std::strtoull(sv.data(), nullptr, 10);
        if (n <= 0) error(std::format("array fill literal: size must be positive, got {}", n));
        const LogosType* elem_type = fill_val->type;
        // Keep IntLit unresolved so that struct-literal type inference (hint_struct_type_)
        // can widen the element to the correct concrete type (e.g. i64 for Vec<i64>).
        std::vector<lir::LExprPtr> elems;
        elems.push_back(std::move(fill_val));
        for (int64_t i = 1; i < n; ++i)
            elems.push_back(lower_expr(val_node));  // re-lower for each slot (simple literals)
        return make_expr(make_array(elem_type, (size_t)n), lir::EArrLit{std::move(elems)});
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
        if (!einfo.type_params.empty()) {
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
            // Fill any still-unresolved type params from hint (e.g. let e: Result<i32,i32> = Result::Err(-1))
            if (hint_enum_type_ && hint_enum_type_->enum_name == std::string(ename)) {
                for (size_t i = 0; i < einfo.type_params.size() && i < hint_enum_type_->type_args.size(); ++i) {
                    if (subst.find(einfo.type_params[i].name) == subst.end()) {
                        auto* hta = hint_enum_type_->type_args[i];
                        if (hta && hta->kind != LogosType::Kind::Error)
                            subst[einfo.type_params[i].name] = hta;
                    }
                }
            }
            // Build concrete type args
            std::vector<const LogosType*> type_args;
            for (auto& tp : einfo.type_params) {
                auto sit = subst.find(tp.name);
                type_args.push_back(sit != subst.end() ? sit->second : error_t());
            }
            check_type_bounds(std::string(ename), einfo.type_params, type_args);
            LogosType et; et.kind = LogosType::Kind::Enum;
            et.enum_name = std::string(ename);
            et.type_args = std::move(type_args);
            result_type = pool_.alloc(std::move(et));
            // Resolve payload types with substitution
            for (size_t i = 0; i < resolved_payload_types.size(); ++i)
                resolved_payload_types[i] = subst_type_sema(resolved_payload_types[i], subst);
        }

        // Type-check payload args against expected types
        if (!vinfo->is_variadic && payload.size() != vinfo->payload_types.size()) {
            error(std::format("{}::{} expects {} args, got {}",
                  ename, vname, vinfo->payload_types.size(), payload.size()));
        } else if (!vinfo->is_variadic) {
            for (size_t i = 0; i < payload.size(); ++i) {
                if (payload[i]->type->kind != LogosType::Kind::Error &&
                    resolved_payload_types[i] &&
                    resolved_payload_types[i]->kind != LogosType::Kind::Error &&
                    !types_compatible(payload[i]->type, resolved_payload_types[i]))
                    error(std::format("{}::{} arg {}: expected {}, got {}",
                          ename, vname, i, type_str(resolved_payload_types[i]),
                          type_str(payload[i]->type)));
            }
        } else {
            // Variadic variant: match each arg against the pack's type (if it's not a generic expansion itself).
            if (!resolved_payload_types.empty()) {
                auto* pack_t = resolved_payload_types[0];
                for (size_t i = 0; i < payload.size(); ++i) {
                    if (payload[i]->type->kind != LogosType::Kind::Error &&
                        pack_t->kind != LogosType::Kind::Error &&
                        !types_compatible(payload[i]->type, pack_t))
                        error(std::format("{}::{} variadic arg {}: expected {}, got {}",
                              ename, vname, i, type_str(pack_t), type_str(payload[i]->type)));
                }
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
        if (!einfo.type_params.empty()) {
            SemaSubst subst;
            for (size_t i = 0; i < vinfo->payload_types.size() && i < payload.size(); ++i) {
                auto* pt = vinfo->payload_types[i];
                if (pt && pt->kind == LogosType::Kind::TypeVar) {
                    auto* inferred = payload[i]->type;
                    if (inferred->kind == LogosType::Kind::IntLit) inferred = i32_t();
                    subst[pt->type_var_name] = inferred;
                }
            }
            // Fill any still-unresolved type params from hint
            if (hint_enum_type_ && hint_enum_type_->enum_name == std::string(ename)) {
                for (size_t i = 0; i < einfo.type_params.size() && i < hint_enum_type_->type_args.size(); ++i) {
                    if (subst.find(einfo.type_params[i].name) == subst.end()) {
                        auto* hta = hint_enum_type_->type_args[i];
                        if (hta && hta->kind != LogosType::Kind::Error)
                            subst[einfo.type_params[i].name] = hta;
                    }
                }
            }
            std::vector<const LogosType*> type_args;
            for (auto& tp : einfo.type_params) {
                auto sit = subst.find(tp.name);
                type_args.push_back(sit != subst.end() ? sit->second : error_t());
            }
            check_type_bounds(std::string(ename), einfo.type_params, type_args);
            LogosType et; et.kind = LogosType::Kind::Enum;
            et.enum_name = std::string(ename);
            et.type_args = std::move(type_args);
            result_type = pool_.alloc(std::move(et));
            for (size_t i = 0; i < resolved_payload_types.size(); ++i)
                resolved_payload_types[i] = subst_type_sema(resolved_payload_types[i], subst);
        }
        if (!vinfo->is_variadic && payload.size() != vinfo->payload_types.size()) {
            error(std::format("{}::{} expects {} args, got {}",
                  ename, vname, vinfo->payload_types.size(), payload.size()));
        } else if (!vinfo->is_variadic) {
            for (size_t i = 0; i < payload.size(); ++i) {
                if (payload[i]->type->kind != LogosType::Kind::Error &&
                    resolved_payload_types[i] &&
                    resolved_payload_types[i]->kind != LogosType::Kind::Error &&
                    !types_compatible(payload[i]->type, resolved_payload_types[i]))
                    error(std::format("{}::{} arg {}: expected {}, got {}",
                          ename, vname, i, type_str(resolved_payload_types[i]),
                          type_str(payload[i]->type)));
            }
        } else {
            if (!resolved_payload_types.empty()) {
                auto* pack_t = resolved_payload_types[0];
                for (size_t i = 0; i < payload.size(); ++i) {
                    if (payload[i]->type->kind != LogosType::Kind::Error &&
                        pack_t->kind != LogosType::Kind::Error &&
                        !types_compatible(payload[i]->type, pack_t))
                        error(std::format("{}::{} variadic arg {}: expected {}, got {}",
                              ename, vname, i, type_str(pack_t), type_str(payload[i]->type)));
                }
            }
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
        for (auto& f : cinfo.all_fields) initialized[f.name] = false;
        for (auto& [fname, fval] : fields) {
            auto it = initialized.find(fname);
            if (it == initialized.end()) {
                error(std::format("'new {}': unknown field '{}'", cname, fname));
            } else {
                it->second = true;
                // Find expected type; skip check if field type is TypeVar (checked post-mono)
                for (auto& f : cinfo.all_fields) {
                    if (f.name == fname && f.type->kind != LogosType::Kind::Error &&
                        f.type->kind != LogosType::Kind::TypeVar &&
                        fval->type->kind != LogosType::Kind::Error &&
                        !compat(fval->type, f.type)) {
                        error(std::format("'new {}' field '{}': expected {}, got {}",
                              cname, fname, type_str(f.type), type_str(fval->type)));
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
                    for (auto& f : cinfo.all_fields) {
                        if (f.name != fname) continue;
                        if (f.type->kind == LogosType::Kind::TypeVar) {
                            auto& tv = f.type->type_var_name;
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
            check_type_bounds(std::string(cname), cinfo.type_params, args);
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

        // Bug 3 fix: look in both funcs_ and generic_funcs_ (generic static methods
        // registered with type params end up in generic_funcs_, not funcs_).
        const SemaFuncInfo* fi_ptr = nullptr;
        {
            auto fit = funcs_.find(mangled);
            if (fit != funcs_.end()) fi_ptr = &fit->second;
            else {
                auto git = generic_funcs_.find(mangled);
                if (git != generic_funcs_.end()) fi_ptr = &git->second;
            }
        }
        if (!fi_ptr) {
            error(std::format("call to undefined static method '{}::{}'", class_name, method_name));
            return make_expr(error_t(), lir::ECall{mangled, {}, std::move(arg_exprs)});
        }

        auto& fi = *fi_ptr;
        check_pub_access(fi.is_pub, fi.package, mangled);
        if (fi.is_unsafe && !inside_unsafe_)
            error(std::format("call to unsafe method '{}' requires unsafe context", mangled));

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
            if (stmts.size() == 0) { error("block-as-expression: empty branch"); return error_expr(); }
            lir::LExprPtr result = nullptr;
            auto block = std::make_unique<lir::LBlock>();
            for (uint64_t i = 0; i < stmts.size(); ++i) {
                auto s = map_of(stmts.get(i));
                if (i == stmts.size() - 1) {
                    int32_t lc = code_of(s);
                    if (lc == la::EXPR_STMT && s.has_key(la::VALUE)) {
                        result = lower_expr(map_of(s.get(la::VALUE.code)));
                    } else if (lc != la::EXPR_STMT && lc != la::LET && lc != la::LET_DESTRUCT && lc != la::RETURN) {
                        result = lower_expr(s);
                    } else {
                        block->stmts.push_back(lower_stmt(s));
                    }
                } else {
                    block->stmts.push_back(lower_stmt(s));
                }
            }
            if (!result) return make_expr(void_t(), lir::EBlockExpr{std::move(block), nullptr});
            const LogosType* rt = result->type;
            return make_expr(rt, lir::EBlockExpr{std::move(block), std::move(result)});
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

        // Determine result type: pick the more concrete type when IntLit vs concrete int.
        const LogosType* result_type = then_val->type;
        if (then_val->type->kind == LogosType::Kind::Error)
            result_type = else_val->type;
        else if (else_val->type->kind != LogosType::Kind::Error) {
            if (!types_compatible(then_val->type, else_val->type) &&
                !types_compatible(else_val->type, then_val->type)) {
                error(std::format("if-expression branches have incompatible types: {} vs {}",
                      type_str(then_val->type), type_str(else_val->type)));
            } else {
                result_type = unify_int(then_val->type, else_val->type);
            }
        }
        // If still IntLit, upgrade to i64 if any branch literal overflows i32.
        if (result_type->kind == LogosType::Kind::IntLit) {
            auto intlit_overflow = [](const lir::LExpr* e) -> bool {
                // Look through block expressions to the final result.
                if (auto* blk = std::get_if<lir::EBlockExpr>(&e->kind))
                    e = blk->result.get();
                if (!e) return false;
                if (auto* lit = std::get_if<lir::ELitInt>(&e->kind))
                    return lit->value > (int64_t)INT32_MAX || lit->value < (int64_t)INT32_MIN;
                return false;
            };
            if (intlit_overflow(then_val.get()) || intlit_overflow(else_val.get()))
                result_type = prim(LogosType::Kind::I64);
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

        // Lower body — closure body is its own unsafe scope, does NOT inherit enclosing context.
        auto saved_ret = ret_type_;
        bool saved_unsafe = inside_unsafe_;
        ret_type_ = ret_type;
        inside_unsafe_ = false;
        lir::LBlock body;
        if (node.has_key(la::BODY)) {
            auto body_node = map_of(node.get(la::BODY.code));
            if (code_of(body_node) == la::BLOCK)
                body = lower_block(body_node);
        }
        ret_type_ = saved_ret;
        inside_unsafe_ = saved_unsafe;
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
        if (c == la::LET_DESTRUCT) return lower_let_destruct(stmt);
        if (c == la::ASSIGN)          return lower_assign(stmt);
        if (c == la::COMPOUND_ASSIGN) return lower_compound_assign(stmt);
        if (c == la::RETURN)       return lower_return(stmt);
        if (c == la::IF)           return lower_if(stmt);
        if (c == la::WHILE)        return lower_while(stmt);
        if (c == la::FOR)          return lower_for(stmt);
        if (c == la::FOR_EACH)     return lower_for_each(stmt);
        if (c == la::LOOP)         return lower_loop(stmt);
        if (c == la::FIELD_WRITE)        return lower_field_write(stmt);
        if (c == la::DEREF_FIELD_WRITE)  return lower_deref_field_write(stmt);
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
        if (c == la::DEREF_WRITE) {
            // *ptr = value;
            if (!inside_unsafe_)
                error("write through raw pointer requires unsafe context");
            lir::LExprPtr ptr = stmt.has_key(la::NAME)
                ? lower_expr(map_of(stmt.get(la::NAME.code)))
                : error_expr();
            lir::LExprPtr val = stmt.has_key(la::VALUE)
                ? lower_expr(map_of(stmt.get(la::VALUE.code)))
                : error_expr();
            auto* pt = ptr->type;
            if (pt->kind != LogosType::Kind::Ptr || !pt->pointee) {
                error("deref-write: '=' left side must be a pointer");
            }
            return make_stmt(node_line_, lir::SDerefWrite{std::move(ptr), std::move(val)});
        }
        if (c == la::DELETE_STMT) {
            if (!inside_unsafe_)
                error("'delete' operation requires unsafe context");
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
        if (c == la::UNSAFE_BLOCK) {
            bool was = inside_unsafe_;
            inside_unsafe_ = true;
            auto inner = stmt.has_key(la::BODY)
                ? lower_block(map_of(stmt.get(la::BODY.code)))
                : lir::LBlock{};
            inside_unsafe_ = was;
            return make_stmt(node_line_, lir::SBlock{std::make_unique<lir::LBlock>(std::move(inner))});
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
                if (s.is_null()) continue;
                auto lowered = lower_stmt(s);
                // Insert drops before return/break/continue
                if (std::holds_alternative<lir::SReturn>(lowered.kind)) {
                    for (auto& d : collect_all_drops())
                        result.stmts.push_back(std::move(d));
                } else if (std::holds_alternative<lir::SBreak>(lowered.kind) ||
                           std::holds_alternative<lir::SContinue>(lowered.kind)) {
                    for (auto& d : collect_drops())
                        result.stmts.push_back(std::move(d));
                }
                result.stmts.push_back(std::move(lowered));
            }
        }
        // Insert drops for normal block exit (no return/break/continue)
        if (result.stmts.empty() ||
            (!std::holds_alternative<lir::SReturn>(result.stmts.back().kind) &&
             !std::holds_alternative<lir::SBreak>(result.stmts.back().kind) &&
             !std::holds_alternative<lir::SContinue>(result.stmts.back().kind))) {
            for (auto& d : collect_drops())
                result.stmts.push_back(std::move(d));
        }
        pop_scope();
        return result;
    }

    // let (a, b, ...) = expr;  — tuple destructuring
    // Expands to: { let __destruct_N = expr; let a = __destruct_N.0; let b = __destruct_N.1; ... }
    lir::LStmt lower_let_destruct(TinyMapView node) {
        lir::LExprPtr rhs = node.has_key(la::VALUE)
            ? lower_expr(map_of(node.get(la::VALUE.code)))
            : error_expr();
        const LogosType* rhs_type = rhs->type;
        if (rhs_type->kind != LogosType::Kind::Tuple) {
            error(std::format("let (...) = ...: right-hand side must be a tuple, got {}",
                  type_str(rhs_type)));
            return make_stmt(node_line_, lir::SExprStmt{std::move(rhs)});
        }

        // Collect binding names
        std::vector<std::string> names;
        if (node.has_key(la::NAMES)) {
            auto nlist = map_of(node.get(la::NAMES.code));
            if (nlist.has_key(la::ITEMS)) {
                auto arr = arr_of(nlist.get(la::ITEMS.code));
                for (uint64_t i = 0; i < arr.size(); ++i) {
                    auto bnode = map_of(arr.get(i));
                    names.push_back(std::string(str_of(bnode.get(la::NAME.code))));
                }
            }
        }
        if (names.size() != rhs_type->tuple_elems.size()) {
            error(std::format("let (...) = ...: expected {} bindings, got {}",
                  rhs_type->tuple_elems.size(), names.size()));
        }

        // Build SBlock: let __destruct_N = rhs; let a = __destruct_N.0; ...
        static int destruct_counter = 0;
        std::string tmp = std::format("__destruct_{}", destruct_counter++);

        auto blk = std::make_unique<lir::LBlock>();

        // let __destruct_N = rhs
        define(tmp, rhs_type);
        lir::SLet tmp_let;
        tmp_let.name    = tmp;
        tmp_let.type    = rhs_type;
        tmp_let.is_mut  = false;
        tmp_let.value   = std::move(rhs);
        blk->stmts.push_back(make_stmt(node_line_, std::move(tmp_let)));

        // let name_i = __destruct_N.i
        for (size_t i = 0; i < names.size() && i < rhs_type->tuple_elems.size(); ++i) {
            auto* elem_t = rhs_type->tuple_elems[i];
            define(names[i], elem_t);

            auto tmp_ref = make_expr(rhs_type, lir::EVarRef{tmp});
            auto elem_expr = make_expr(elem_t, lir::ETupleIndex{std::move(tmp_ref), (uint32_t)i});

            lir::SLet elem_let;
            elem_let.name   = names[i];
            elem_let.type   = elem_t;
            elem_let.is_mut = false;
            elem_let.value  = std::move(elem_expr);
            blk->stmts.push_back(make_stmt(node_line_, std::move(elem_let)));
        }

        lir::SBlock sb;
        sb.body = std::move(blk);
        return make_stmt(node_line_, std::move(sb));
    }

    lir::LStmt lower_let(TinyMapView node) {
        auto name = str_of(node.get(la::NAME.code));
        bool is_mut = false;
        if (node.has_key(la::IS_MUT)) {
            AnyVal av = node.get(la::IS_MUT.code);
            if (!av.is_null() && av.is_value()) is_mut = av.as_value<uint8_t>() != 0;
        }

        // Parse type annotation first so we can use it as a hint for enum literal inference
        const LogosType* ann = nullptr;
        if (node.has_key(la::TYPE))
            ann = resolve_type(map_of(node.get(la::TYPE.code)));

        // Set enum/struct hints so literal lowering can fill in unresolved type params
        auto* saved_hint = hint_enum_type_;
        if (ann && ann->kind == LogosType::Kind::Enum && !ann->type_args.empty())
            hint_enum_type_ = ann;
        auto* saved_struct_hint = hint_struct_type_;
        if (ann && ann->kind == LogosType::Kind::Struct && !ann->type_args.empty())
            hint_struct_type_ = ann;

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

        hint_enum_type_ = saved_hint;
        hint_struct_type_ = saved_struct_hint;

        const LogosType* var_type;
        if (ann != nullptr) {
            if (ann->kind != LogosType::Kind::Error &&
                rhs_type->kind != LogosType::Kind::Error &&
                !types_compatible(rhs_type, ann)) {
                error(std::format("let '{}': type mismatch — expected {}, got {}",
                      name, type_str(ann), type_str(rhs_type)));
            }
            var_type = ann;
        } else {
            var_type = rhs_type;
            if (var_type->kind == LogosType::Kind::IntLit) {
                // Default IntLit to i32; upgrade to i64 if the literal value overflows i32.
                var_type = i32_t();
                if (auto* lit = std::get_if<lir::ELitInt>(&rhs->kind))
                    if (lit->value > (int64_t)INT32_MAX || lit->value < (int64_t)INT32_MIN)
                        var_type = prim(LogosType::Kind::I64);
            }
        }

        define(name, var_type, is_mut);

        // Move semantics: if RHS is a variable reference to a move type, mark it moved
        if (rhs && is_move_type(rhs_type)) {
            if (auto* vr = std::get_if<lir::EVarRef>(&rhs->kind))
                mark_moved(vr->name);
        }

        lir::SLet slet;
        slet.name   = std::string(name);
        slet.type   = var_type;
        slet.is_mut = is_mut;
        slet.value  = std::move(rhs);
        return make_stmt(node_line_, std::move(slet));
    }

    lir::LStmt lower_compound_assign(TinyMapView node) {
        auto name = str_of(node.get(la::NAME.code));
        auto op_tok = str_of(node.get(la::OP.code));
        // Strip trailing '=' to get the base operator
        std::string base_op;
        if (op_tok.size() >= 2 && op_tok.back() == '=')
            base_op = std::string(op_tok.substr(0, op_tok.size() - 1));
        else
            base_op = std::string(op_tok);  // fallback

        auto* var_type = lookup(name);
        if (!var_type) {
            error(std::format("compound assignment to undefined variable '{}'", name));
            if (node.has_key(la::VALUE)) lower_expr(map_of(node.get(la::VALUE.code)));
            return make_stmt(node_line_, lir::SBreak{});
        }
        if (!lookup_is_mut(name))
            error(std::format("compound assignment to immutable variable '{}'", name));

        // Desugar: `x op= expr` → `x = x op expr`
        auto lhs_ref = make_expr(var_type, lir::EVarRef{std::string(name)});
        auto rhs = node.has_key(la::VALUE)
            ? lower_expr(map_of(node.get(la::VALUE.code))) : error_expr();

        // Synthesize the binop LIR node
        auto binop = make_expr(var_type, lir::EBinOp{base_op, std::move(lhs_ref), std::move(rhs)});
        return make_stmt(node_line_, lir::SAssign{std::string(name), std::move(binop)});
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
        // Re-assignment revives the variable (the old value was already consumed).
        moved_vars_.erase(std::string(name));

        return make_stmt(node_line_, lir::SAssign{std::string(name), std::move(rhs)});
    }

    lir::LStmt lower_return(TinyMapView node) {
        lir::LExprPtr val;
        if (node.has_key(la::VALUE)) {
            AnyVal vav = node.get(la::VALUE.code);
            if (!vav.is_null()) {
                // Set enum hint from return type so enum literals can fill in unresolved type params
                auto* saved_hint = hint_enum_type_;
                if (ret_type_ && ret_type_->kind == LogosType::Kind::Enum && !ret_type_->type_args.empty())
                    hint_enum_type_ = ret_type_;
                val = lower_expr(map_of(vav));
                hint_enum_type_ = saved_hint;
                if (ret_type_ && ret_type_->kind == LogosType::Kind::ImplTrait) {
                    // Infer concrete return type from first return expression.
                    if (!impl_ret_type_inferred_ &&
                        val->type->kind != LogosType::Kind::Error)
                        impl_ret_type_inferred_ = val->type;
                } else if (ret_type_ && ret_type_->kind != LogosType::Kind::Error &&
                    val->type->kind != LogosType::Kind::Error &&
                    !compat(val->type, ret_type_)) {
                    error(std::format("return type mismatch — expected {}, got {}",
                          type_str(ret_type_), type_str(val->type)));
                }
                return make_stmt(node_line_, lir::SReturn{std::move(val)});
            }
        }
        // void return
        if (ret_type_ && ret_type_->kind != LogosType::Kind::Void &&
            ret_type_->kind != LogosType::Kind::Error &&
            ret_type_->kind != LogosType::Kind::ImplTrait) {
            error(std::format("return without value in function returning {}",
                  type_str(ret_type_)));
        }
        return make_stmt(node_line_, lir::SReturn{nullptr});
    }

    // Build a LIR pattern from a pattern AST node + scrutinee type.
    // Shared by lower_match, lower_if_let, lower_while_let.
    lir::Pattern build_pattern(TinyMapView pnode, const LogosType* scrut_type) {
        int32_t pc = code_of(pnode);
        if (pc == la::PAT_VARIANT) {
            auto pename = std::string(str_of(pnode.get(la::NAME.code)));
            auto pvname = std::string(str_of(pnode.get(la::FIELD.code)));
            int32_t disc = 0;
            auto eit = enums_.find(pename);
            if (eit == enums_.end()) {
                error(std::format("pattern: unknown enum '{}'", pename));
            } else {
                if (scrut_type->kind == LogosType::Kind::Enum &&
                    scrut_type->enum_name != pename)
                    error(std::format("pattern: enum '{}' != scrutinee '{}'",
                          pename, type_str(scrut_type)));
                bool found = false;
                for (auto& v : eit->second.variants)
                    if (v.name == pvname) { disc = v.value; found = true; break; }
                if (!found)
                    error(std::format("pattern: enum '{}' has no variant '{}'", pename, pvname));
            }
            return lir::PatVariant{pename, pvname, disc};
        }
        if (pc == la::PAT_VARIANT_DATA) {
            auto pename = std::string(str_of(pnode.get(la::NAME.code)));
            auto pvname = std::string(str_of(pnode.get(la::FIELD.code)));
            int32_t disc = 0;
            const SemaVariantInfo* vinfo = nullptr;
            auto eit = enums_.find(pename);
            if (eit == enums_.end()) {
                error(std::format("pattern: unknown enum '{}'", pename));
            } else {
                for (auto& v : eit->second.variants)
                    if (v.name == pvname) { vinfo = &v; disc = v.value; break; }
                if (!vinfo)
                    error(std::format("pattern: enum '{}' has no variant '{}'", pename, pvname));
            }
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
            std::vector<const LogosType*> binding_types;
            if (vinfo) {
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
            if (bindings.size() != binding_types.size())
                error(std::format("pattern {}::{}: expected {} bindings, got {}",
                      pename, pvname, binding_types.size(), bindings.size()));
            return lir::PatVariantData{pename, pvname, disc,
                                       std::move(bindings), std::move(binding_types)};
        }
        if (pc == la::PAT_INT) {
            auto sv = str_of(pnode.get(la::VALUE.code));
            return lir::PatInt{(int64_t)std::strtoll(sv.data(), nullptr, 10)};
        }
        if (pc == la::PAT_BOOL) {
            AnyVal bv = pnode.get(la::VALUE.code);
            bool bval = !bv.is_null() && bv.is_value() && bv.as_value<uint8_t>();
            return lir::PatBool{bval};
        }
        // PAT_WILD or fallback
        auto wname = str_of(pnode.get(la::NAME.code));
        return lir::PatWild{std::string(wname)};
    }

    // Push pattern bindings into current scope (call after push_scope()).
    // scrut_type is needed to bind named wildcards (e.g. `n` in `n if n > 0 =>`).
    void bind_pattern(const lir::Pattern& pat,
                      const LogosType* scrut_type = nullptr) {
        if (auto* pvd = std::get_if<lir::PatVariantData>(&pat)) {
            for (size_t i = 0; i < pvd->bindings.size() &&
                                i < pvd->binding_types.size(); ++i)
                define(pvd->bindings[i], pvd->binding_types[i]);
        } else if (auto* pw = std::get_if<lir::PatWild>(&pat)) {
            if (pw->name != "_" && scrut_type)
                define(pw->name, scrut_type);
        }
    }

    lir::LStmt lower_if(TinyMapView node) {
        // ── if let pattern = expr { ... } ─────────────────────────────
        if (node.has_key(la::PAT)) {
            auto scrut = node.has_key(la::VALUE)
                ? lower_expr(map_of(node.get(la::VALUE.code))) : error_expr();
            const LogosType* scrut_type = scrut->type;

            auto pat = build_pattern(map_of(node.get(la::PAT.code)), scrut_type);

            // Then arm: pattern → then block
            push_scope();
            bind_pattern(pat, scrut_type);
            lir::LBlockPtr then_body = std::make_unique<lir::LBlock>();
            if (node.has_key(la::THEN))
                *then_body = lower_block(map_of(node.get(la::THEN.code)));
            pop_scope();

            // Else arm: wildcard → else block (or empty)
            lir::LBlockPtr else_body = std::make_unique<lir::LBlock>();
            if (node.has_key(la::ELSE)) {
                auto else_node = map_of(node.get(la::ELSE.code));
                if (code_of(else_node) == la::BLOCK) {
                    *else_body = lower_block(else_node);
                } else {
                    // else if: wrap in block
                    else_body->stmts.push_back(lower_if(else_node));
                }
            }

            lir::SMatch sm;
            sm.scrut = std::move(scrut);
            sm.arms.push_back({std::move(pat), std::move(then_body), std::nullopt});
            sm.arms.push_back({lir::PatWild{"_"}, std::move(else_body), std::nullopt});
            return make_stmt(node_line_, std::move(sm));
        }

        // ── regular if cond { ... } ────────────────────────────────────
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
        // ── while let pattern = expr { ... } ──────────────────────────
        // Desugars to: loop { match expr { PAT => body, _ => break } }
        if (node.has_key(la::PAT)) {
            auto scrut = node.has_key(la::VALUE)
                ? lower_expr(map_of(node.get(la::VALUE.code))) : error_expr();
            const LogosType* scrut_type = scrut->type;

            auto pat = build_pattern(map_of(node.get(la::PAT.code)), scrut_type);

            // Then arm: pattern → loop body
            push_scope();
            bind_pattern(pat, scrut_type);
            lir::LBlockPtr then_body = std::make_unique<lir::LBlock>();
            if (node.has_key(la::BODY)) {
                ++loop_depth_;
                *then_body = lower_block(map_of(node.get(la::BODY.code)));
                --loop_depth_;
            }
            pop_scope();

            // Else arm: wildcard → break
            lir::LBlockPtr else_body = std::make_unique<lir::LBlock>();
            else_body->stmts.push_back(make_stmt(node_line_, lir::SBreak{}));

            lir::SMatch sm;
            sm.scrut = std::move(scrut);
            sm.arms.push_back({std::move(pat), std::move(then_body), std::nullopt});
            sm.arms.push_back({lir::PatWild{"_"}, std::move(else_body), std::nullopt});

            auto loop_body = std::make_unique<lir::LBlock>();
            loop_body->stmts.push_back(make_stmt(node_line_, std::move(sm)));
            lir::SLoop sl; sl.body = std::move(loop_body);
            return make_stmt(node_line_, std::move(sl));
        }

        // ── regular while cond { ... } ─────────────────────────────────
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
    // ── for item in iterator — desugared to while-let loop ──────────
    lir::LStmt lower_for_each(TinyMapView node) {
        auto var_name = str_of(node.get(la::NAME.code));

        lir::LExprPtr iter = node.has_key(la::ITER)
            ? lower_expr(map_of(node.get(la::ITER.code))) : error_expr();

        const LogosType* iter_type = iter->type;

        // ── array path (original) ────────────────────────────────────
        if (iter_type->kind == LogosType::Kind::Array) {
            int64_t arr_size = (int64_t)iter_type->arr_size;
            const LogosType* elem_type = iter_type->elem ? iter_type->elem : i32_t();

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

        // ── slice path: &[T] — iterate by index over fat pointer ────────
        if (iter_type->kind == LogosType::Kind::Slice) {
            const LogosType* elem_type = iter_type->elem ? iter_type->elem : i32_t();
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
            sfe.arr_size  = 0;
            sfe.is_slice  = true;
            sfe.body      = std::move(body);
            return make_stmt(node_line_, std::move(sfe));
        }

        // ── iterator path: desugar to while-let loop ─────────────────
        // Requires: iter_type has a `next()` method returning Option<T>
        // Desugars: for x in iter { body }
        //        → { let mut __iter = iter; while let Opt::Some(x) = __iter.next() { body } }
        if (iter_type->kind != LogosType::Kind::Error) {
            auto sname = struct_name_from_type(iter_type);
            if (sname.empty()) {
                error(std::format("for-in: '{}' is not iterable (not a struct)", type_str(iter_type)));
                return make_stmt(node_line_, lir::SBreak{});
            }

            auto mangled_next = std::string(sname) + "__next";
            const SemaFuncInfo* fi_ptr = nullptr;
            if (auto fit = funcs_.find(mangled_next); fit != funcs_.end()) fi_ptr = &fit->second;
            else if (auto git = generic_funcs_.find(mangled_next); git != generic_funcs_.end()) fi_ptr = &git->second;

            if (!fi_ptr) {
                error(std::format("for-in: type '{}' has no `next()` method", sname));
                return make_stmt(node_line_, lir::SBreak{});
            }

            // next() must return an enum (Option-like)
            const LogosType* next_ret = fi_ptr->ret_type;
            // Substitute type args if iterator is generic
            if (!iter_type->type_args.empty()) {
                auto sit = structs_.find(std::string(sname));
                if (sit != structs_.end()) {
                    SemaSubst subst;
                    auto& tps = sit->second.type_params;
                    for (size_t i = 0; i < tps.size() && i < iter_type->type_args.size(); ++i)
                        subst[tps[i].name] = iter_type->type_args[i];
                    next_ret = subst_type_sema(next_ret, subst);
                }
            }
            if (next_ret->kind != LogosType::Kind::Enum) {
                error(std::format("for-in: `{}.next()` must return an enum, got {}",
                      sname, type_str(next_ret)));
                return make_stmt(node_line_, lir::SBreak{});
            }

            // Find the payload variant (Some-like: first variant with payload)
            const SemaVariantInfo* some_variant = nullptr;
            auto eit = enums_.find(next_ret->enum_name);
            if (eit == enums_.end()) {
                error(std::format("for-in: enum '{}' not found", next_ret->enum_name));
                return make_stmt(node_line_, lir::SBreak{});
            }
            for (auto& v : eit->second.variants)
                if (!v.payload_types.empty()) { some_variant = &v; break; }
            if (!some_variant) {
                error(std::format("for-in: enum '{}' has no payload variant", next_ret->enum_name));
                return make_stmt(node_line_, lir::SBreak{});
            }

            // Resolve element type (substitute generics from next_ret's type_args)
            const LogosType* elem_type = some_variant->payload_types[0];
            if (!next_ret->type_args.empty()) {
                SemaSubst subst;
                auto& tps = eit->second.type_params;
                for (size_t i = 0; i < tps.size() && i < next_ret->type_args.size(); ++i)
                    subst[tps[i].name] = next_ret->type_args[i];
                elem_type = subst_type_sema(elem_type, subst);
            }

            // Synthesize: let mut __iter = iter
            std::string iter_var = "__for_iter_" + std::to_string(tmp_var_count_++);
            lir::SLet let_iter;
            let_iter.name   = iter_var;
            let_iter.type   = iter_type;
            let_iter.is_mut = true;
            let_iter.value  = std::move(iter);

            // Build outer block: { let mut __iter = iter; loop { match __iter.next() ... } }
            auto outer_block = std::make_unique<lir::LBlock>();
            outer_block->stmts.push_back(make_stmt(node_line_, std::move(let_iter)));

            // Synthesize __iter.next() call expression (inside the loop)
            auto make_next_call = [&]() -> lir::LExprPtr {
                auto iter_ref = make_expr(iter_type, lir::EVarRef{iter_var});
                return make_expr(next_ret,
                    lir::EMethodCall{std::move(iter_ref), "next", {}, {}, -1, ""});
            };

            // Then arm: Some(x) → body
            lir::PatVariantData some_pat;
            some_pat.enum_name = next_ret->enum_name;
            some_pat.variant   = some_variant->name;
            some_pat.disc         = some_variant->value;
            some_pat.bindings     = {std::string(var_name)};
            some_pat.binding_types = {elem_type};

            push_scope();
            define(iter_var, iter_type, true);
            define(std::string(var_name), elem_type, false);
            auto then_body = std::make_unique<lir::LBlock>();
            if (node.has_key(la::BODY)) {
                ++loop_depth_;
                *then_body = lower_block(map_of(node.get(la::BODY.code)));
                --loop_depth_;
            }
            pop_scope();

            // Else arm: _ → break
            auto else_body = std::make_unique<lir::LBlock>();
            else_body->stmts.push_back(make_stmt(node_line_, lir::SBreak{}));

            lir::SMatch sm;
            sm.scrut = make_next_call();
            sm.arms.push_back({lir::Pattern{std::move(some_pat)}, std::move(then_body), std::nullopt});
            sm.arms.push_back({lir::PatWild{"_"}, std::move(else_body), std::nullopt});

            auto loop_body = std::make_unique<lir::LBlock>();
            loop_body->stmts.push_back(make_stmt(node_line_, std::move(sm)));
            lir::SLoop sl; sl.body = std::move(loop_body);
            outer_block->stmts.push_back(make_stmt(node_line_, std::move(sl)));

            // Wrap in a block statement
            return make_stmt(node_line_, lir::SBlock{std::move(outer_block)});
        }

        return make_stmt(node_line_, lir::SBreak{});
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
            } else if (recv_type && recv_type->kind == LogosType::Kind::Ref) {
                error(std::format("field write to '{}': receiver is &T (shared reference)", recv_name));
            } else if (!is_class && !lookup_is_mut(recv_name) &&
                       !(recv_type && recv_type->kind == LogosType::Kind::MutRef)) {
                error(std::format("field write to immutable variable '{}'", recv_name));
            }
        }
        const LogosType* recv_struct_t = (sname.empty() && cname.empty()) ? nullptr : lookup(recv_name);
        if (recv_struct_t && recv_struct_t->kind == LogosType::Kind::Ptr) {
            if (!inside_unsafe_)
                error("field write through raw pointer requires unsafe context");
            recv_struct_t = recv_struct_t->pointee;
        } else if (recv_struct_t && is_ref_like(recv_struct_t->kind)) {
            recv_struct_t = recv_struct_t->pointee;
        }
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
        // Pub check for struct field writes.
        if (!sname.empty() && ft) {
            auto sit = structs_.find(std::string(sname));
            if (sit != structs_.end()) {
                for (auto& f : sit->second.fields) {
                    if (f.name == field_name) {
                        check_pub_access(f.is_pub, sit->second.package, field_name);
                        break;
                    }
                }
            }
        }
        // Pub check for class field writes.
        if (!cname.empty() && ft) {
            auto cit = classes_.find(std::string(cname));
            if (cit != classes_.end()) {
                for (auto& f : cit->second.all_fields) {
                    if (f.name == field_name) {
                        check_pub_access(f.is_pub, cit->second.package, field_name);
                        break;
                    }
                }
            }
        }

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

    lir::LStmt lower_deref_field_write(TinyMapView node) {
        if (!inside_unsafe_)
            error("write through raw pointer field requires unsafe context");
        auto recv_name  = str_of(node.get(la::RECEIVER.code));
        auto field_name = str_of(node.get(la::FIELD.code));

        const LogosType* ptr_type = lookup(recv_name);
        if (!ptr_type) {
            error(std::format("deref-field-write: undefined variable '{}'", recv_name));
        } else if (!is_ref_like(ptr_type->kind) || !ptr_type->pointee) {
            error(std::format("deref-field-write: '{}' is not a pointer or reference (got {})",
                              recv_name, type_str(ptr_type)));
        } else if (ptr_type->kind == LogosType::Kind::Ptr && !ptr_type->mut_ptr) {
            error(std::format("deref-field-write: '{}' is a *const pointer (need *mut)",
                              recv_name));
        } else if (ptr_type->kind == LogosType::Kind::Ref) {
            error(std::format("deref-field-write: '{}' is a &T (shared reference, need &mut T)",
                              recv_name));
        }

        const LogosType* pointee = (ptr_type && ptr_type->pointee) ? ptr_type->pointee : nullptr;
        std::string type_name;
        const LogosType* ft = nullptr;
        if (pointee) {
            if (pointee->kind == LogosType::Kind::Class) {
                type_name = pointee->struct_name;
                ft = class_field_type(type_name, field_name);
            } else if (pointee->kind == LogosType::Kind::Struct) {
                type_name = concrete_struct_name(pointee);
                ft = field_type_of_for_type(pointee, field_name);
            } else {
                error(std::format("deref-field-write: '{}' points to non-struct/class type {}",
                                  recv_name, type_str(pointee)));
            }
        }
        if (pointee && ft == nullptr) {
            error(std::format("deref-field-write: type '{}' has no field '{}'",
                              type_name, field_name));
        }
        // Pub check for struct fields.
        if (pointee && pointee->kind == LogosType::Kind::Struct && ft) {
            auto sit = structs_.find(std::string(pointee->struct_name));
            if (sit != structs_.end()) {
                for (auto& f : sit->second.fields) {
                    if (f.name == field_name) {
                        check_pub_access(f.is_pub, sit->second.package, field_name);
                        break;
                    }
                }
            }
        }
        // Pub check for class fields.
        if (pointee && pointee->kind == LogosType::Kind::Class && ft) {
            auto cit = classes_.find(std::string(type_name));
            if (cit != classes_.end()) {
                for (auto& f : cit->second.all_fields) {
                    if (f.name == field_name) {
                        check_pub_access(f.is_pub, cit->second.package, field_name);
                        break;
                    }
                }
            }
        }

        lir::LExprPtr val = node.has_key(la::VALUE)
            ? lower_expr(map_of(node.get(la::VALUE.code)))
            : error_expr();
        if (ft && ft->kind != LogosType::Kind::Error &&
            val->type->kind != LogosType::Kind::Error &&
            !types_compatible(val->type, ft)) {
            error(std::format("deref-field-write '(*{}).{}': expected {}, got {}",
                  recv_name, field_name, type_str(ft), type_str(val->type)));
        }

        lir::SDerefFieldWrite sdfw;
        sdfw.receiver  = std::string(recv_name);
        sdfw.type_name = type_name;
        sdfw.field     = std::string(field_name);
        sdfw.value     = std::move(val);
        return make_stmt(node_line_, std::move(sdfw));
    }

    lir::LStmt lower_index_write(TinyMapView node) {
        auto arr_name = str_of(node.get(la::NAME.code));
        auto* arr_type = lookup(arr_name);
        if (!arr_type) {
            error(std::format("index write: undefined variable '{}'", arr_name));
        } else if (arr_type->kind != LogosType::Kind::Array &&
                   arr_type->kind != LogosType::Kind::Ptr &&
                   arr_type->kind != LogosType::Kind::Ref &&
                   arr_type->kind != LogosType::Kind::MutRef &&
                   arr_type->kind != LogosType::Kind::Error) {
            error(std::format("index write: '{}' is not an array or pointer (got {})",
                  arr_name, type_str(arr_type)));
        } else if (arr_type->kind == LogosType::Kind::Array && !lookup_is_mut(arr_name)) {
            error(std::format("index write to immutable array '{}'", arr_name));
        } else if (arr_type->kind == LogosType::Kind::Ptr && !arr_type->mut_ptr) {
            error(std::format("index write through *const pointer '{}'", arr_name));
        } else if (arr_type->kind == LogosType::Kind::Ptr && !inside_unsafe_) {
            error(std::format("index write through raw pointer '{}' requires unsafe context", arr_name));
        } else if (arr_type->kind == LogosType::Kind::Ref) {
            error(std::format("index write through &T (shared reference) '{}'", arr_name));
        }

        lir::LExprPtr idx = node.has_key(la::LHS)
            ? lower_expr(map_of(node.get(la::LHS.code))) : error_expr();
        if (!is_integer(idx->type))
            error(std::format("array index must be an integer, got {}", type_str(idx->type)));

        const LogosType* elem_type = nullptr;
        if (arr_type) {
            if (arr_type->kind == LogosType::Kind::Array) elem_type = arr_type->elem;
            else if (arr_type->kind == LogosType::Kind::Ptr ||
                     arr_type->kind == LogosType::Kind::Ref ||
                     arr_type->kind == LogosType::Kind::MutRef) elem_type = arr_type->pointee;
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

        // Unwrap pointer/reference receiver (class/struct-via-ptr/ref).
        const LogosType* base_t = recv_t;
        if (base_t && is_ref_like(base_t->kind)) base_t = base_t->pointee;

        const LogosType* field_t = nullptr;
        if (base_t) {
            auto sname = struct_name_from_type(base_t);
            auto cname = class_name_from_type(base_t);
            if (!sname.empty())       field_t = field_type_of_for_type(base_t, field_name);
            else if (!cname.empty())  field_t = class_field_type(cname, field_name);
        }

        if (!field_t)
            error(std::format("field index write: cannot resolve field '{}.{}'", recv_name, field_name));

        // Pub check for struct fields.
        if (base_t && field_t) {
            auto sname = struct_name_from_type(base_t);
            if (!sname.empty()) {
                auto sit = structs_.find(std::string(sname));
                if (sit != structs_.end()) {
                    for (auto& f : sit->second.fields) {
                        if (f.name == field_name) {
                            check_pub_access(f.is_pub, sit->second.package, field_name);
                            break;
                        }
                    }
                }
            }
            // Pub check for class fields.
            auto cname = class_name_from_type(base_t);
            if (!cname.empty()) {
                auto cit = classes_.find(std::string(cname));
                if (cit != classes_.end()) {
                    for (auto& f : cit->second.all_fields) {
                        if (f.name == field_name) {
                            check_pub_access(f.is_pub, cit->second.package, field_name);
                            break;
                        }
                    }
                }
            }
        }

        if (field_t && field_t->kind != LogosType::Kind::Ptr &&
                       field_t->kind != LogosType::Kind::Ref &&
                       field_t->kind != LogosType::Kind::MutRef &&
                       field_t->kind != LogosType::Kind::Array)
            error(std::format("field index write: field '{}.{}' is not a pointer/reference or array (got {})",
                  recv_name, field_name, type_str(field_t)));
        if (field_t && field_t->kind == LogosType::Kind::Ptr && !field_t->mut_ptr)
            error(std::format("field index write: field '{}.{}' is *const, cannot write",
                  recv_name, field_name));
        if (field_t && field_t->kind == LogosType::Kind::Ptr && field_t->mut_ptr && !inside_unsafe_)
            error(std::format("field index write '{}.{}[i]' through raw pointer requires unsafe context",
                  recv_name, field_name));
        if (field_t && field_t->kind == LogosType::Kind::Ref)
            error(std::format("field index write: field '{}.{}' is &T (shared reference), cannot write",
                  recv_name, field_name));

        const LogosType* elem_t = nullptr;
        if (field_t) {
            if (field_t->kind == LogosType::Kind::Ptr ||
                field_t->kind == LogosType::Kind::Ref ||
                field_t->kind == LogosType::Kind::MutRef) elem_t = field_t->pointee;
            else if (field_t->kind == LogosType::Kind::Array) elem_t = field_t->elem;
        }

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
                lir::Pattern pat = arm.has_key(la::LHS)
                    ? build_pattern(map_of(arm.get(la::LHS.code)), scrut_type)
                    : lir::PatWild{"_"};

                // Build body block — push pattern bindings into scope
                push_scope();
                bind_pattern(pat, scrut_type);

                // Optional guard: `pattern if expr =>`
                std::optional<lir::LExprPtr> guard;
                if (arm.has_key(la::GUARD)) {
                    auto g = lower_expr(map_of(arm.get(la::GUARD.code)));
                    if (g->type->kind != LogosType::Kind::Bool &&
                        g->type->kind != LogosType::Kind::Error)
                        error("match guard must be bool");
                    guard = std::move(g);
                }

                lir::LBlockPtr body = std::make_unique<lir::LBlock>();
                if (arm.has_key(la::BODY)) {
                    auto body_node = map_of(arm.get(la::BODY.code));
                    if (code_of(body_node) == la::BLOCK) {
                        *body = lower_block(body_node);
                    } else {
                        body->stmts.push_back(lower_stmt(body_node));
                    }
                } else if (arm.has_key(la::EXPR)) {
                    auto val = lower_expr(map_of(arm.get(la::EXPR.code)));
                    if (match_in_tail_position_) {
                        // Tail-position match: EXPR arms produce the function's return value.
                        lir::SReturn ret; ret.value = std::move(val);
                        body->stmts.push_back(make_stmt(node_line_, std::move(ret)));
                    } else {
                        // Statement-position match: EXPR arms are evaluated for side effects.
                        lir::SExprStmt es; es.expr = std::move(val);
                        body->stmts.push_back(make_stmt(node_line_, std::move(es)));
                    }
                }
                pop_scope();
                smatch.arms.push_back({std::move(pat), std::move(body), std::move(guard)});
            }
        }
        // ── Exhaustiveness check ─────────────────────────────────
        // Verify all variants of an enum (or bool) are covered.
        {
            bool has_wild = false;
            for (auto& arm : smatch.arms) {
                if (std::holds_alternative<lir::PatWild>(arm.pat)) {
                    has_wild = true;
                    break;
                }
            }
            if (!has_wild && scrut_type->kind == LogosType::Kind::Enum) {
                auto eit = enums_.find(scrut_type->enum_name);
                if (eit != enums_.end()) {
                    std::set<int32_t> covered;
                    for (auto& arm : smatch.arms) {
                        if (auto* pv = std::get_if<lir::PatVariant>(&arm.pat))
                            covered.insert(pv->disc);
                        else if (auto* pvd = std::get_if<lir::PatVariantData>(&arm.pat))
                            covered.insert(pvd->disc);
                    }
                    std::string missing;
                    for (auto& v : eit->second.variants) {
                        if (covered.find(v.value) == covered.end()) {
                            if (!missing.empty()) missing += ", ";
                            missing += std::string(v.name);
                        }
                    }
                    if (!missing.empty())
                        error(std::format("match is not exhaustive — missing variant(s): {}",
                              missing));
                }
            }
            if (!has_wild && scrut_type->kind == LogosType::Kind::Bool) {
                bool has_true = false, has_false = false;
                for (auto& arm : smatch.arms) {
                    if (auto* pb = std::get_if<lir::PatBool>(&arm.pat)) {
                        if (pb->value) has_true = true; else has_false = true;
                    }
                }
                if (!has_true || !has_false)
                    error("match on bool is not exhaustive — missing "
                          + std::string(!has_true ? "true" : "false"));
            }
        }

        return make_stmt(node_line_, std::move(smatch));
    }

    // ── lower_match_expr: match used as an expression (all arms have EXPR) ──
    lir::LExprPtr lower_match_expr(TinyMapView node) {
        lir::LExprPtr scrut;
        const LogosType* scrut_type = error_t();
        if (node.has_key(la::VALUE)) {
            scrut = lower_expr(map_of(node.get(la::VALUE.code)));
            scrut_type = scrut->type;
        } else { scrut = error_expr(); }

        // Check that all arms use EXPR (expression body).
        // Note: ITEMS includes the scrutinee as first element; skip non-MATCH_ARM.
        bool all_expr = true;
        if (node.has_key(la::ITEMS)) {
            auto arms = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < arms.size(); ++i) {
                auto arm = map_of(arms.get(i));
                if (code_of(arm) != la::MATCH_ARM) continue;
                if (!arm.has_key(la::EXPR)) { all_expr = false; break; }
            }
        }
        if (!all_expr) {
            error("match as expression requires all arms to have expression bodies (pattern => expr,)");
            return error_expr();
        }

        lir::EMatchExpr me;
        me.scrut = std::move(scrut);
        const LogosType* result_type = error_t();

        if (node.has_key(la::ITEMS)) {
            auto arms = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < arms.size(); ++i) {
                auto arm = map_of(arms.get(i));
                if (code_of(arm) != la::MATCH_ARM) continue;

                lir::Pattern pat = arm.has_key(la::LHS)
                    ? build_pattern(map_of(arm.get(la::LHS.code)), scrut_type)
                    : lir::PatWild{"_"};

                push_scope();
                bind_pattern(pat, scrut_type);

                std::optional<lir::LExprPtr> guard;
                if (arm.has_key(la::GUARD)) {
                    auto g = lower_expr(map_of(arm.get(la::GUARD.code)));
                    if (g->type->kind != LogosType::Kind::Bool &&
                        g->type->kind != LogosType::Kind::Error)
                        error("match guard must be bool");
                    guard = std::move(g);
                }

                lir::LExprPtr val = lower_expr(map_of(arm.get(la::EXPR.code)));
                if (result_type->kind == LogosType::Kind::Error) {
                    result_type = val->type;
                } else if (val->type->kind != LogosType::Kind::Error) {
                    if (!types_compatible(val->type, result_type) &&
                        !types_compatible(result_type, val->type)) {
                        error(std::format(
                            "match expression: arm type '{}' is incompatible with '{}'",
                            type_str(val->type), type_str(result_type)));
                    } else {
                        result_type = unify_int(result_type, val->type);
                    }
                }
                // Upgrade IntLit result to i64 if any arm literal overflows i32.
                if (result_type->kind == LogosType::Kind::IntLit) {
                    const lir::LExpr* ve = val.get();
                    if (auto* blk = std::get_if<lir::EBlockExpr>(&ve->kind))
                        ve = blk->result.get();
                    if (ve) {
                        if (auto* lit = std::get_if<lir::ELitInt>(&ve->kind))
                            if (lit->value > (int64_t)INT32_MAX || lit->value < (int64_t)INT32_MIN)
                                result_type = prim(LogosType::Kind::I64);
                    }
                }

                pop_scope();
                lir::EMatchArm ema;
                ema.pat   = std::move(pat);
                ema.guard = std::move(guard);
                ema.value = std::move(val);
                me.arms.push_back(std::move(ema));
            }
        }

        return make_expr(result_type, std::move(me));
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

        SemaFuncInfo* fi_ptr = nullptr;
        {
            // If this AST node actually has type params, prefer generic_funcs_.
            auto node_tparams = read_type_params(node);
            if (!node_tparams.empty()) {
                auto git = generic_funcs_.find(mangled);
                if (git != generic_funcs_.end()) fi_ptr = &git->second;
            }
            if (!fi_ptr) {
                auto it = funcs_.find(mangled);
                if (it != funcs_.end()) fi_ptr = &it->second;
            }
            if (!fi_ptr) {
                auto git = generic_funcs_.find(mangled);
                if (git != generic_funcs_.end()) fi_ptr = &git->second;
            }
        }
        if (!fi_ptr) return fn;   // shouldn't happen after collect

        fn.type_params    = fi_ptr->type_params;
        fn.lifetime_params = read_lifetime_params(node);
        // Robust associated type resolution: call subst_type_sema even if subst is empty
        // to simplify concrete AssocType nodes (e.g. i32::Item -> bool).
        fn.ret_type    = subst_type_sema(fi_ptr->ret_type, {});
        ret_type_      = fn.ret_type;
        // Reset impl-trait inference state for this function.
        if (fn.ret_type && fn.ret_type->kind == LogosType::Kind::ImplTrait)
            impl_ret_type_inferred_ = nullptr;

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
                        auto ptype = (i < fi_ptr->param_types.size())
                            ? fi_ptr->param_types[i] : error_t();
                        bool p_variadic = false;
                        if (p.has_key(la::IS_VARIADIC)) {
                            AnyVal av = p.get(la::IS_VARIADIC.code);
                            p_variadic = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
                        }
                        // Simplify parameter type too
                        const LogosType* pt = subst_type_sema(ptype, {});
                        define(pname, pt);
                        fn.params.push_back({std::string(pname), pt, p_variadic});
                    }
                }
            }
        }

        // unsafe fn body is implicitly an unsafe context
        bool was_unsafe = inside_unsafe_;
        if (fi_ptr->is_unsafe) inside_unsafe_ = true;

        // Body (extern fns have no body)
        if (!fn.is_extern && node.has_key(la::BODY)) {
            auto body_node = map_of(node.get(la::BODY.code));
            // Detect if the last stmt in the function body is a match.
            // If so, set the flag so lower_match treats EXPR arms as return values.
            if (fn.ret_type && fn.ret_type->kind != LogosType::Kind::Void) {
                if (body_node.has_key(la::ITEMS)) {
                    auto stmts = arr_of(body_node.get(la::ITEMS.code));
                    // Find last non-null stmt
                    for (int64_t si = (int64_t)stmts.size() - 1; si >= 0; --si) {
                        auto s = map_of(stmts.get(si));
                        if (!s.is_null()) {
                            match_in_tail_position_ = (code_of(s) == la::MATCH);
                            break;
                        }
                    }
                }
            }
            fn.body = lower_block(body_node);
            match_in_tail_position_ = false;
            // Resolve impl Trait return type to the concrete type inferred from returns.
            if (fn.ret_type && fn.ret_type->kind == LogosType::Kind::ImplTrait) {
                if (impl_ret_type_inferred_) {
                    fn.ret_type       = impl_ret_type_inferred_;
                    fi_ptr->ret_type  = impl_ret_type_inferred_;
                    ret_type_         = impl_ret_type_inferred_;
                } else {
                    error("impl Trait return: could not infer concrete return type");
                }
            }
            // Return reachability check (on AST node — before scope is gone)
            if (fn.ret_type && fn.ret_type->kind != LogosType::Kind::Void &&
                fn.ret_type->kind != LogosType::Kind::Error &&
                !block_always_returns(body_node)) {
                error("not all paths return a value");
            }
        }

        inside_unsafe_ = was_unsafe;
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
            sd.fields.push_back({std::string(f.name), f.type, f.is_variadic});
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
            ed.variants.push_back({std::string(v.name), v.value, v.payload_types, v.is_variadic});
        return ed;
    }

    // ── Const fn evaluator ──────────────────────────────────────────────────
    // Simple tree-walking interpreter on the AST; only handles integer arithmetic,
    // local lets, if/else, and return.  Returns nullopt if not foldable.

    using ConstEnv = std::unordered_map<std::string, int64_t>;

    std::optional<int64_t> const_eval_expr(TinyMapView e, const ConstEnv& env) {
        if (!e.ptr()) return std::nullopt;
        auto c = code_of(e);

        if (c == la::LIT_INT) {
            auto sv = str_of(e.get(la::VALUE.code));
            return (int64_t)std::strtoull(sv.data(), nullptr, 10);
        }
        if (c == la::LIT_BOOL) {
            auto sv = str_of(e.get(la::VALUE.code));
            return sv == "true" ? (int64_t)1 : (int64_t)0;
        }
        if (c == la::VAR_REF) {
            auto name = str_of(e.get(la::NAME.code));
            auto it = env.find(std::string(name));
            return it != env.end() ? std::optional<int64_t>(it->second) : std::nullopt;
        }
        if (c == la::PAREN_EXPR)
            return const_eval_expr(map_of(e.get(la::VALUE.code)), env);
        if (c == la::CAST)
            return const_eval_expr(map_of(e.get(la::VALUE.code)), env);
        if (c == la::UNARY) {
            auto op  = str_of(e.get(la::OP.code));
            auto val = const_eval_expr(map_of(e.get(la::VALUE.code)), env);
            if (!val) return std::nullopt;
            if (op == "-") return -*val;
            if (op == "!") return *val ? (int64_t)0 : (int64_t)1;
            return std::nullopt;
        }
        if (c == la::BINOP) {
            auto op = str_of(e.get(la::OP.code));
            auto l  = const_eval_expr(map_of(e.get(la::LHS.code)), env);
            auto r  = const_eval_expr(map_of(e.get(la::RHS.code)), env);
            if (!l || !r) return std::nullopt;
            if (op == "+")  return *l + *r;
            if (op == "-")  return *l - *r;
            if (op == "*")  return *l * *r;
            if (op == "/" && *r != 0) return *l / *r;
            if (op == "%" && *r != 0) return *l % *r;
            if (op == "<")  return *l < *r  ? (int64_t)1 : (int64_t)0;
            if (op == ">")  return *l > *r  ? (int64_t)1 : (int64_t)0;
            if (op == "<=") return *l <= *r ? (int64_t)1 : (int64_t)0;
            if (op == ">=") return *l >= *r ? (int64_t)1 : (int64_t)0;
            if (op == "==") return *l == *r ? (int64_t)1 : (int64_t)0;
            if (op == "!=") return *l != *r ? (int64_t)1 : (int64_t)0;
            if (op == "&&") return (*l && *r) ? (int64_t)1 : (int64_t)0;
            if (op == "||") return (*l || *r) ? (int64_t)1 : (int64_t)0;
            return std::nullopt;
        }
        if (c == la::IF) {
            auto cond = e.has_key(la::COND)
                ? const_eval_expr(map_of(e.get(la::COND.code)), env)
                : std::nullopt;
            if (!cond) return std::nullopt;
            AnyVal branch = *cond ? e.get(la::THEN.code) : e.get(la::ELSE.code);
            if (branch.is_null()) return std::nullopt;
            return const_eval_block(map_of(branch), env);
        }
        // Nested const fn call: CALL node with a known const fn callee.
        if (c == la::CALL) {
            auto callee_name = str_of(e.get(la::CALLEE.code));
            auto fit = funcs_.find(std::string(callee_name));
            if (fit == funcs_.end() || !fit->second.is_const) return std::nullopt;
            auto bit = const_fn_bodies_.find(std::string(callee_name));
            if (bit == const_fn_bodies_.end()) return std::nullopt;
            ConstEnv call_env;
            if (e.has_key(la::ARGS)) {
                auto args_arr = arr_of(e.get(la::ARGS.code));
                auto& pnames = bit->second.param_names;
                if (args_arr.size() != pnames.size()) return std::nullopt;
                for (uint64_t i = 0; i < args_arr.size(); ++i) {
                    auto av = const_eval_expr(map_of(args_arr.get(i)), env);
                    if (!av) return std::nullopt;
                    call_env[pnames[i]] = *av;
                }
            }
            return const_eval_block(bit->second.body, call_env);
        }
        return std::nullopt;
    }

    std::optional<int64_t> const_eval_block(TinyMapView block, ConstEnv env) {
        if (!block.ptr() || !block.has_key(la::ITEMS)) return std::nullopt;
        auto stmts = arr_of(block.get(la::ITEMS.code));
        for (uint64_t i = 0; i < stmts.size(); ++i) {
            auto stmt = map_of(stmts.get(i));
            auto c = code_of(stmt);
            if (c == la::RETURN) {
                if (stmt.has_key(la::VALUE))
                    return const_eval_expr(map_of(stmt.get(la::VALUE.code)), env);
                return (int64_t)0;
            }
            if (c == la::LET) {
                auto name = str_of(stmt.get(la::NAME.code));
                if (!stmt.has_key(la::VALUE)) return std::nullopt;
                auto v = const_eval_expr(map_of(stmt.get(la::VALUE.code)), env);
                if (!v) return std::nullopt;
                env[std::string(name)] = *v;
                continue;
            }
            if (c == la::IF) {
                auto cond = stmt.has_key(la::COND)
                    ? const_eval_expr(map_of(stmt.get(la::COND.code)), env)
                    : std::nullopt;
                if (!cond) return std::nullopt;
                if (*cond) {
                    auto res = const_eval_block(map_of(stmt.get(la::THEN.code)), env);
                    if (res) return res;  // returned from branch
                } else if (stmt.has_key(la::ELSE)) {
                    auto res = const_eval_block(map_of(stmt.get(la::ELSE.code)), env);
                    if (res) return res;
                }
                continue;
            }
            // Other statements not supported in const context — give up
            return std::nullopt;
        }
        return std::nullopt;
    }

    // Try to evaluate a CALL expression as a const fn call.
    // Returns a folded integer literal LExprPtr on success, or nullptr.
    lir::LExprPtr try_const_fold_call(TinyMapView call_node) {
        if (!call_node.ptr() || code_of(call_node) != la::CALL) return nullptr;
        auto callee_name = str_of(call_node.get(la::CALLEE.code));
        auto fit = funcs_.find(std::string(callee_name));
        if (fit == funcs_.end() || !fit->second.is_const) return nullptr;
        auto bit = const_fn_bodies_.find(std::string(callee_name));
        if (bit == const_fn_bodies_.end()) return nullptr;

        // Evaluate arguments as constants.
        ConstEnv env;
        if (call_node.has_key(la::ARGS)) {
            auto args_arr = arr_of(call_node.get(la::ARGS.code));
            auto& pnames = bit->second.param_names;
            if (args_arr.size() != pnames.size()) return nullptr;
            for (uint64_t i = 0; i < args_arr.size(); ++i) {
                auto arg_val = const_eval_expr(map_of(args_arr.get(i)), {});
                if (!arg_val) return nullptr;
                env[pnames[i]] = *arg_val;
            }
        }

        auto result = const_eval_block(bit->second.body, env);
        if (!result) return nullptr;
        return make_expr(i32_t(), lir::ELitInt{*result});
    }

    lir::LConst lower_const_def(TinyMapView node) {
        auto name = std::string(str_of(node.get(la::NAME.code)));
        lir::LConst lc;
        lc.name = name;
        auto cit = module_consts_.find(name);
        lc.type = (cit != module_consts_.end()) ? cit->second : error_t();
        if (node.has_key(la::VALUE)) {
            auto val_node = map_of(node.get(la::VALUE.code));
            // Try to fold const fn calls at compile time.
            if (auto folded = try_const_fold_call(val_node)) {
                lc.value = std::move(folded);
            } else {
                lc.value = lower_expr(val_node);
            }
        } else {
            lc.value = error_expr();
        }
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
            for (auto& at : tit->second.assoc_types)
                td.assoc_types.push_back({at.name, at.bounds});
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
        // Push impl's own type params: either from IMPL_TYPE_PARAMS (new generic trait impl
        // form: impl<T> Trait for Struct<T>) or from TYPE_PARAMS (standalone: impl<T> Pair<T>).
        std::vector<TypeParam> impl_tps;
        if (node.has_key(la::IMPL_TYPE_PARAMS)) {
            impl_tps = read_type_params_from(node, la::IMPL_TYPE_PARAMS.code);
            push_type_params(impl_tps);
            impl_type_params_ = impl_tps;
        } else if (trait_name.empty() && node.has_key(la::TYPE_PARAMS)) {
            impl_tps = read_type_params(node);
            push_type_params(impl_tps);
            impl_type_params_ = impl_tps;  // so lower_fn includes them in fn.type_params
        }
        std::string target;
        if (node.has_key(la::TYPE)) {
            auto tnode = map_of(node.get(la::TYPE.code));
            if (code_of(tnode) == la::PTR_TYPE) {
                auto* resolved = resolve_type(tnode);
                target = type_str(resolved);
            } else if (code_of(tnode) == la::GENERIC_INST) {
                target = std::string(str_of(tnode.get(la::NAME.code)));
                if (impl_tps.empty()) {
                    auto* resolved = resolve_type(tnode);
                    if (resolved && !resolved->type_args.empty()) {
                        bool concrete = true;
                        for (auto* a : resolved->type_args)
                            if (a && a->kind == LogosType::Kind::TypeVar) { concrete = false; break; }
                        if (concrete) {
                            if (resolved->kind == LogosType::Kind::Struct)
                                target = concrete_struct_name(resolved);
                            else if (resolved->kind == LogosType::Kind::Class)
                                target = concrete_class_name(resolved);
                        }
                    }
                }
            } else {
                target = std::string(str_of(tnode.get(la::NAME.code)));
            }
        }
        lir::LImplBlock ib;
        ib.trait_name   = trait_name;
        ib.target_type  = target;
        // Resolve trait type args and push into scope
        if (!trait_name.empty() && node.has_key(la::TYPE_PARAMS)) {
            AnyVal tpav = node.get(la::TYPE_PARAMS.code);
            if (!tpav.is_null()) {
                auto tplist = map_of(tpav);
                if (tplist.has_key(la::ITEMS)) {
                    auto items = arr_of(tplist.get(la::ITEMS.code));
                    auto tit = traits_.find(trait_name);
                    for (uint64_t i = 0; i < items.size(); ++i) {
                        auto resolved = resolve_type(map_of(items.get(i)));
                        if (tit != traits_.end() && i < tit->second.type_params.size())
                            current_type_params_[tit->second.type_params[i].name] = resolved;
                    }
                }
            }
        }
        // Lower impl methods as free functions (Target__method).
        // For `impl<T> GenericClass<T>` blocks, add methods to the class template instead of
        // prog.functions so mono's instantiate_one_class can clone them with T substituted.
        lir::LClassDef*  target_class_tmpl  = nullptr;
        lir::LStructDef* target_struct_tmpl = nullptr;
        if (!impl_tps.empty()) {
            for (auto& cd : prog.classes)
                if (cd.name == target) { target_class_tmpl = &cd; break; }
            if (!target_class_tmpl)
                for (auto& sd : prog.structs)
                    if (sd.name == target) { target_struct_tmpl = &sd; break; }
        }
        std::unordered_set<std::string> overridden;
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto m = map_of(items.get(i));
                if (code_of(m) == la::FN || code_of(m) == la::STATIC_FN) {
                    auto fn = lower_fn(m, target);
                    overridden.insert(fn.name);
                    if (target_class_tmpl) {
                        // Add to class template so mono clones it during class instantiation.
                        fn.type_params.clear();  // T is provided by the class template
                        target_class_tmpl->methods.push_back(std::move(fn));
                    } else if (target_struct_tmpl) {
                        // Same for struct templates.
                        fn.type_params.clear();
                        target_struct_tmpl->methods.push_back(std::move(fn));
                    } else {
                        prog.functions.push_back(std::move(fn));
                    }
                }
            }
        }
        // Lower default methods from the trait that weren't overridden.
        if (!trait_name.empty()) {
            auto tit = traits_.find(trait_name);
            if (tit != traits_.end()) {
                for (auto& m : tit->second.methods) {
                    auto mangled = target + "__" + m.name;
                    if (m.has_default && !overridden.count(mangled)) {
                        // Push Self → target type; for generic impls include type params as TypeVars.
                        const LogosType* self_type = nullptr;
                        if (structs_.count(target)) {
                            if (!impl_tps.empty()) {
                                std::vector<const LogosType*> tv_args;
                                for (auto& tp : impl_tps)
                                    tv_args.push_back(make_typevar(tp.name));
                                self_type = make_generic_struct(target, std::move(tv_args));
                            } else {
                                self_type = make_struct_type(target);
                            }
                        } else if (classes_.count(target)) {
                            self_type = make_ptr(true, make_class_type(target));
                        }
                        if (self_type)
                            current_type_params_["Self"] = self_type;
                        auto fn = lower_fn(map_of(m.default_ast), target);
                        prog.functions.push_back(std::move(fn));
                        current_type_params_.erase("Self");
                    }
                }
            }
        }
        // Clean up trait type params
        if (!trait_name.empty()) {
            auto tit = traits_.find(trait_name);
            if (tit != traits_.end()) {
                for (auto& tp : tit->second.type_params)
                    current_type_params_.erase(tp.name);
            }
        }
        // Clean up impl's own type params
        if (!impl_tps.empty()) { pop_type_params(impl_tps); impl_type_params_.clear(); }
        // Copy associated type mappings
        if (!trait_name.empty()) {
            auto prefix = trait_name + "::" + target + "::";
            for (auto& [key, entry] : assoc_type_impls_) {
                if (key.rfind(prefix, 0) == 0) {
                    auto assoc_name = key.substr(prefix.size());
                    ib.assoc_types[assoc_name] = entry.type;
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
            auto& f = cinfo.all_fields[i];
            cd.own_fields.push_back({f.name, f.type, f.is_variadic});
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
            cur_package_ = read_package_name(root);
            lower_module_items(root, prog);
        }
        cur_package_ = {};
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
