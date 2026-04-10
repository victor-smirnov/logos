// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// SemaChecker class definition — included by all sema_*.cpp translation units.
//
// All method bodies for large methods are defined in:
//   sema.cpp        — run(), primitives, helpers, type subst, resolve_type, lower_program
//   sema_collect.cpp — collect_*, finalize_classes, collect_fn
//   sema_expr.cpp   — lower_expr, lower_*_expr, lower_call, lower_method_call, lower_*_lit
//   sema_stmt.cpp   — lower_stmt, lower_let, lower_assign, lower_*, build_pattern
//   sema_decl.cpp   — lower_fn, lower_struct_def, lower_enum_def, lower_class_def, lower_*_def

#pragma once

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
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace logos::compiler {

// ── Forward declarations of free helpers used in inline class methods ─────
// (Full definitions are in sema.cpp / sema_impl.hpp bottom section.)

// Defined in sema.cpp (non-inline, single definition):
bool types_equal(const LogosType& a, const LogosType& b) noexcept;
std::string type_str(const LogosType* t);
std::string concrete_struct_name(const LogosType* t);
std::string concrete_class_name(const LogosType* t);
bool types_compatible(const LogosType* from, const LogosType* to) noexcept;

// Inline (defined below, after class, so visible in all TUs):
inline bool is_integer_kind(LogosType::Kind k) noexcept;

// Aliases used throughout SemaChecker method implementations.
// Placed here so all sema_*.cpp files get them automatically.
namespace sema_detail {
    namespace la = logos::compiler::ast;
    using hermes::TinyMapView;
    using hermes::ArrayView;
    using hermes::StringView;
    using hermes::AnyVal;
    using hermes::MemHolder;
}

class SemaChecker {
public:
    lir::LProgram run(const std::vector<hermes::HermesCtr>& asts,
                      const std::vector<std::string>& filenames);

private:
    // ── Type pool and primitives ─────────────────────────────────

    TypePool pool_;

    // prims_[int(Kind)] for primitive kinds.  Class and TypeVar are not primitives.
    // Size = int(Kind::Error) + 1 to cover all Kind values.
    std::array<const LogosType*, int(LogosType::Kind::Error) + 1> prims_{};

    void init_primitives();

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

    const LogosType* lookup_type_by_name(std::string_view name);

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

    uint32_t get_line(hermes::TinyMapView node) noexcept {
        using namespace sema_detail;
        if (node.is_null()) return 0;
        AnyVal av = node.get(la::SRC_LINE.code);
        if (av.is_null() || !av.is_value()) return 0;
        return av.as_value<uint32_t>();
    }

    // ── Hermes helpers ───────────────────────────────────────────

    hermes::MemHolder* holder_ = nullptr;

    int32_t code_of(hermes::TinyMapView node) noexcept {
        using namespace sema_detail;
        if (node.is_null()) return -1;
        AnyVal av = node.get(la::CODE.code);
        return av.is_null() ? -1 : av.as_value<int32_t>();
    }

    std::string_view str_of(hermes::AnyVal av) noexcept {
        using namespace sema_detail;
        if (av.is_null()) return {};
        return StringView(av.to_offset(), holder_).view();
    }

    hermes::TinyMapView map_of(hermes::AnyVal av) noexcept {
        using namespace sema_detail;
        if (av.is_null()) return TinyMapView{};
        return TinyMapView(av.to_offset(), holder_);
    }

    hermes::ArrayView arr_of(hermes::AnyVal av) noexcept {
        using namespace sema_detail;
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

    bool is_move_type(const LogosType* t) const;
    void mark_moved(const std::string& name) { moved_vars_.insert(name); }

    std::string drop_fn_for(const LogosType* t) const;
    bool has_droppable_fields(const LogosType* t) const;
    bool needs_drop(const LogosType* t) const {
        return !drop_fn_for(t).empty() || has_droppable_fields(t);
    }

    std::optional<lir::LStmt> make_drop_stmt(const std::string& name, const VarInfo& info) const;
    std::vector<lir::LStmt> collect_drops() const;
    std::vector<lir::LStmt> collect_all_drops() const;

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

    std::string_view struct_name_of(std::string_view var_name);
    std::string_view class_name_of(std::string_view var_name);
    std::string_view struct_name_from_type(const LogosType* t);
    std::string_view class_name_from_type(const LogosType* t);

    // Check if `derived` is the same as or a subclass of `base`.
    bool is_subclass(std::string_view derived, std::string_view base) const {
        if (derived == base) return true;
        auto it = classes_.find(std::string(derived));
        if (it == classes_.end()) return false;
        if (it->second.parent_name.empty()) return false;
        return is_subclass(it->second.parent_name, base);
    }

    const LogosType* class_field_type(std::string_view cname, std::string_view fname) const;
    int32_t vtable_index_of(std::string_view cname, std::string_view mangled_method) const;

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
        hermes::AnyVal default_ast{};  // AST node for default method (valid when has_default)
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
    struct ConstFnBody { hermes::TinyMapView body; std::vector<std::string> param_names; };
    std::unordered_map<std::string, ConstFnBody>      const_fn_bodies_;
    std::unordered_map<std::string, const LogosType*> type_aliases_;
    std::unordered_map<std::string, const LogosType*> module_consts_;
    std::unordered_map<std::string, SemaTraitInfo>    traits_;
    // "TraitName::TypeName" → impl info
    std::unordered_map<std::string, SemaImplInfo>     impls_;
    // "TraitName::TypeName::AssocName" → assoc type + impl's own type params for substitution.
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

    std::vector<std::string> read_lifetime_params(hermes::TinyMapView node);
    std::vector<TypeParam> read_type_params_from(hermes::TinyMapView node, int32_t field_code);
    std::vector<TypeParam> read_type_params(hermes::TinyMapView node);

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

    const LogosType* subst_type_sema(const LogosType* t, const SemaSubst& s);

    // ── Compatibility with class hierarchy ───────────────────────
    bool compat(const LogosType* from, const LogosType* to) const {
        if (types_compatible(from, to)) return true;
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

    const LogosType* resolve_type(hermes::TinyMapView node);

    // ── Collection phase ─────────────────────────────────────────

    void collect(const std::vector<hermes::HermesCtr>& asts);
    void simplify_all_types();
    std::string read_package_name(hermes::TinyMapView mod);
    void check_pub_access(bool is_pub, const std::string& def_package,
                          std::string_view item_name);
    void check_type_bounds(const std::string& target_name,
                           const std::vector<TypeParam>& type_params,
                           const std::vector<const LogosType*>& args);
    void collect_module(hermes::TinyMapView mod, int phase);
    void collect_enum(hermes::TinyMapView node);
    void collect_type_alias(hermes::TinyMapView node);
    void collect_const(hermes::TinyMapView node);
    void collect_trait(hermes::TinyMapView node);
    void collect_impl(hermes::TinyMapView node);
    void collect_struct_spec(hermes::TinyMapView node);
    void collect_class(hermes::TinyMapView node);
    void finalize_classes();
    void collect_struct(hermes::TinyMapView node);
    const LogosType* try_resolve_as_known_type(std::string_view name);
    bool is_known_type_name(std::string_view name) const;
    void extract_typevars_from_type_node(hermes::TinyMapView node,
                                         std::vector<TypeParam>& out);
    bool is_specialization_fn(hermes::TinyMapView node);
    bool is_specialization_struct(hermes::TinyMapView node);
    lir::LStructDef lower_spec_struct(hermes::TinyMapView node);
    lir::LFunction lower_spec_fn(hermes::TinyMapView node);
    void collect_fn(hermes::TinyMapView node, std::string_view struct_ctx = {});

    // ── Loop depth / return type ─────────────────────────────────

    int loop_depth_ = 0;
    bool inside_unsafe_ = false;
    const LogosType* ret_type_ = nullptr;
    bool match_in_tail_position_ = false;
    const LogosType* impl_ret_type_inferred_ = nullptr;
    const LogosType* hint_enum_type_ = nullptr;
    const LogosType* hint_struct_type_ = nullptr;

    // ── Return reachability ───────────────────────────────────────

    bool stmt_always_returns(hermes::TinyMapView stmt);
    bool block_always_returns(hermes::TinyMapView block);

    // ── Lowering helpers ─────────────────────────────────────────

    static bool is_numeric(const LogosType* t) noexcept {
        if (!t) return false;
        return t->kind == LogosType::Kind::F64 ||
               t->kind == LogosType::Kind::F32 ||
               t->kind == LogosType::Kind::FloatLit ||
               t->kind == LogosType::Kind::TypeVar ||
               is_integer_kind(t->kind);
    }
    static bool is_integer(const LogosType* t) noexcept {
        return t && is_integer_kind(t->kind);
    }

    const LogosType* field_type_of(std::string_view sname, std::string_view fname);
    const LogosType* field_type_of_for_type(const LogosType* struct_t, std::string_view fname);

    // ── lower_expr ───────────────────────────────────────────────

    lir::LExprPtr lower_expr(hermes::TinyMapView expr);
    lir::LExprPtr lower_binop(hermes::TinyMapView node);
    lir::LExprPtr lower_unary(hermes::TinyMapView node);
    lir::LExprPtr lower_deref(hermes::TinyMapView node);
    lir::LExprPtr lower_call(hermes::TinyMapView node);
    void unify_types(const LogosType* formal, const LogosType* actual,
                     std::unordered_map<std::string, const LogosType*>& bindings);
    bool infer_type_args(const SemaFuncInfo& fi,
                         const std::vector<lir::LExprPtr>& arg_exprs,
                         std::vector<const LogosType*>& out_type_args,
                         const SemaSubst& context = {},
                         size_t param_offset = 0);
    lir::LExprPtr finish_generic_call(std::string_view callee_sv,
                                      const SemaFuncInfo& fi,
                                      std::vector<const LogosType*> type_args,
                                      std::vector<lir::LExprPtr> arg_exprs);
    lir::LExprPtr lower_generic_call(hermes::TinyMapView node);
    lir::LExprPtr lower_class_method_call(lir::LExprPtr recv,
                                           std::string_view cname,
                                           std::string_view method_name,
                                           hermes::TinyMapView node);
    lir::LExprPtr lower_method_call(hermes::TinyMapView node);
    lir::LExprPtr lower_field_read(hermes::TinyMapView node);
    lir::LExprPtr lower_struct_lit(hermes::TinyMapView node);
    lir::LExprPtr lower_index_read(hermes::TinyMapView node);
    lir::LExprPtr lower_arr_lit(hermes::TinyMapView node);
    lir::LExprPtr lower_arr_fill_lit(hermes::TinyMapView node);
    lir::LExprPtr lower_enum_lit(hermes::TinyMapView node);
    lir::LExprPtr lower_enum_lit_data(hermes::TinyMapView node);
    lir::LExprPtr lower_enum_lit_data_from_static(
            hermes::TinyMapView node, std::string_view ename, std::string_view vname);
    lir::LExprPtr lower_new_expr(hermes::TinyMapView node);
    lir::LExprPtr lower_static_call(hermes::TinyMapView node);
    lir::LExprPtr lower_if_expr(hermes::TinyMapView node);
    lir::LExprPtr lower_closure_expr(hermes::TinyMapView node);

    // ── lower_stmt and friends ───────────────────────────────────

    lir::LStmt lower_stmt(hermes::TinyMapView stmt);
    lir::LBlock lower_block(hermes::TinyMapView block);
    lir::LStmt lower_let_destruct(hermes::TinyMapView node);
    lir::LStmt lower_let(hermes::TinyMapView node);
    lir::LStmt lower_compound_assign(hermes::TinyMapView node);
    lir::LStmt lower_assign(hermes::TinyMapView node);
    lir::LStmt lower_return(hermes::TinyMapView node);
    lir::Pattern build_pattern(hermes::TinyMapView pnode, const LogosType* scrut_type);
    void bind_pattern(const lir::Pattern& pat,
                      const LogosType* scrut_type = nullptr);
    lir::LStmt lower_if(hermes::TinyMapView node);
    lir::LStmt lower_while(hermes::TinyMapView node);
    lir::LStmt lower_for(hermes::TinyMapView node);
    lir::LStmt lower_for_each(hermes::TinyMapView node);
    lir::LStmt lower_loop(hermes::TinyMapView node);
    lir::LStmt lower_field_write(hermes::TinyMapView node);
    lir::LStmt lower_tuple_field_write(hermes::TinyMapView node);
    lir::LStmt lower_deref_field_write(hermes::TinyMapView node);
    lir::LStmt lower_index_write(hermes::TinyMapView node);
    lir::LStmt lower_field_index_write(hermes::TinyMapView node);
    lir::LStmt lower_match(hermes::TinyMapView node);
    lir::LExprPtr lower_match_expr(hermes::TinyMapView node);

    // ── lower_fn and declaration lowering ───────────────────────

    lir::LFunction lower_fn(hermes::TinyMapView node, std::string_view struct_ctx = {});
    lir::LStructDef lower_struct_def(hermes::TinyMapView node);
    lir::LEnumDef lower_enum_def(hermes::TinyMapView node);
    using ConstEnv = std::unordered_map<std::string, int64_t>;
    std::optional<int64_t> const_eval_expr(hermes::TinyMapView e, const ConstEnv& env);
    std::optional<int64_t> const_eval_block(hermes::TinyMapView block, ConstEnv env);
    lir::LExprPtr try_const_fold_call(hermes::TinyMapView call_node);
    lir::LConst lower_const_def(hermes::TinyMapView node);
    lir::LTypeAlias lower_type_alias_def(hermes::TinyMapView node);
    lir::LTraitDef lower_trait_def(hermes::TinyMapView node);
    void lower_impl_block(hermes::TinyMapView node, lir::LProgram& prog);
    lir::LClassDef lower_class_def(hermes::TinyMapView node);
    void lower_program(const std::vector<hermes::HermesCtr>& asts, lir::LProgram& prog);
    void lower_module_items(hermes::TinyMapView mod, lir::LProgram& prog);
};

// ── File-scope helpers used across sema_*.cpp TUs ─────────────────────────
// These are declared/defined inline so each TU gets a copy without ODR
// violations.  They were previously file-static in the anonymous namespace.

inline bool is_integer_kind(LogosType::Kind k) noexcept {
    return k == LogosType::Kind::I32   || k == LogosType::Kind::I64 ||
           k == LogosType::Kind::U8    || k == LogosType::Kind::I8  ||
           k == LogosType::Kind::I16   || k == LogosType::Kind::U16 ||
           k == LogosType::Kind::U32   || k == LogosType::Kind::U64 ||
           k == LogosType::Kind::IntLit || k == LogosType::Kind::Enum;
}

inline const LogosType* unify_int(const LogosType* a, const LogosType* b) noexcept {
    if (a->kind == LogosType::Kind::IntLit) return b;
    return a;
}

inline std::optional<int64_t> get_intlit_value(const lir::LExpr* e) noexcept {
    if (!e) return std::nullopt;
    if (auto* blk = std::get_if<lir::EBlockExpr>(&e->kind))
        e = blk->result.get();
    if (!e) return std::nullopt;
    if (auto* u = std::get_if<lir::EUnary>(&e->kind)) {
        if (u->op == "-") {
            auto inner = get_intlit_value(u->operand.get());
            if (inner) return -(*inner);
        }
        return std::nullopt;
    }
    if (auto* lit = std::get_if<lir::ELitInt>(&e->kind))
        return lit->value;
    return std::nullopt;
}

inline bool intlit_fits(int64_t v, LogosType::Kind k) noexcept {
    switch (k) {
    case LogosType::Kind::I8:  return v >= -128 && v <= 127;
    case LogosType::Kind::U8:  return v >= 0 && v <= 255;
    case LogosType::Kind::I16: return v >= -32768 && v <= 32767;
    case LogosType::Kind::U16: return v >= 0 && v <= 65535;
    case LogosType::Kind::I32: return v >= (int64_t)INT32_MIN && v <= (int64_t)INT32_MAX;
    case LogosType::Kind::U32: return v >= 0 && (uint64_t)v <= (uint64_t)UINT32_MAX;
    default: return true; // i64, u64: all int64_t values fit
    }
}

inline int64_t parse_int_literal(std::string_view sv) noexcept {
    bool negative = !sv.empty() && sv[0] == '-';
    if (negative) sv = sv.substr(1);
    int64_t result = 0;
    if (sv.size() >= 2 && sv[0] == '0') {
        if (sv[1] == 'x' || sv[1] == 'X') {
            for (char c : sv.substr(2)) {
                if (c >= '0' && c <= '9')      result = result * 16 + (c - '0');
                else if (c >= 'a' && c <= 'f') result = result * 16 + (c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') result = result * 16 + (c - 'A' + 10);
                else break;
            }
        } else if (sv[1] == 'b' || sv[1] == 'B') {
            for (char c : sv.substr(2)) {
                if (c == '0' || c == '1') result = result * 2 + (c - '0');
                else break;
            }
        } else if (sv[1] == 'o' || sv[1] == 'O') {
            for (char ch : sv.substr(2)) {
                if (ch >= '0' && ch <= '7') result = result * 8 + (ch - '0');
                else break;
            }
        } else {
            for (char c : sv) {
                if (c >= '0' && c <= '9') result = result * 10 + (c - '0');
                else break;
            }
        }
    } else {
        for (char c : sv) {
            if (c >= '0' && c <= '9') result = result * 10 + (c - '0');
            else break;
        }
    }
    return negative ? -result : result;
}

} // namespace logos::compiler
