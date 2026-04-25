// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// SemaChecker class definition — included by all sema_*.cpp translation units.
//
// All method bodies for large methods are defined in:
//   sema.cpp        — run(), primitives, helpers, type subst, resolve_type, lower_program
//   sema_collect.cpp — collect_*, collect_fn
//   sema_expr.cpp   — lower_expr, lower_*_expr, lower_call, lower_method_call, lower_*_lit
//   sema_stmt.cpp   — lower_stmt, lower_let, lower_assign, lower_*, build_pattern
//   sema_decl.cpp   — lower_fn, lower_struct_def, lower_enum_def, lower_*_def

#pragma once

#include <logos/compiler/lir.hpp>
#include <logos/compiler/ast.hpp>
#include <logos/compiler/sha256.hpp>
#include <logos/compiler/str_map.hpp>
#include <logos/hermes/view.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/any_val.hpp>

#include <cctype>
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
bool types_equal(TypeRef a, TypeRef b) noexcept;
std::string type_str(TypeRef t);
std::string concrete_struct_name(TypeRef t);
bool types_compatible(TypeRef from, TypeRef to) noexcept;

// Inline (defined below, after class, so visible in all TUs):
inline bool is_integer_kind(LogosType::Kind k) noexcept;
inline int64_t parse_int_literal(std::string_view sv) noexcept;

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
    lir::LProgram run(const std::vector<hermes::Hermes>& asts,
                      const std::vector<std::string>& filenames,
                      const std::vector<bool>& from_binary = {});

    void set_metaprog_options(bool mode, size_t entry_ast_idx) {
        metaprog_mode_ = mode;
        metaprog_entry_ast_idx_ = entry_ast_idx;
    }

private:
    // ── Type pool and primitives ─────────────────────────────────

    TypePool pool_;

    // prims_[int(Kind)] for primitive kinds.  TypeVar is not a primitive.
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
        LogosTypeBuilder t; t.kind = LogosType::Kind::Ptr;
        t.mut_ptr = mut; t.pointee = pointee;
        return pool_.alloc(t);
    }
    const LogosType* make_ref(bool mut, const LogosType* pointee, std::string lifetime = "") {
        LogosTypeBuilder t;
        t.kind = mut ? LogosType::Kind::MutRef : LogosType::Kind::Ref;
        t.pointee = pointee;
        t.lifetime = std::move(lifetime);
        return pool_.alloc(std::move(t));
    }
    const LogosType* make_array(const LogosType* elem, uint64_t n, std::string_view symbolic = "") {
        LogosTypeBuilder t; t.kind = LogosType::Kind::Array;
        t.elem = elem; t.arr_size = n;
        t.arr_size_var = std::string(symbolic);
        return pool_.alloc(std::move(t));
    }
    const LogosType* make_struct_type(std::string_view name, std::string_view pkg = {}) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::Struct; t.struct_name = name;
        if (!pkg.empty()) t.pkg_name = std::string(pkg);
        return pool_.alloc(std::move(t));
    }
    const LogosType* make_datatype_type(std::string_view name, std::string_view pkg = {}) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::ZonedStruct; t.struct_name = std::string(name);
        if (!pkg.empty()) t.pkg_name = std::string(pkg);
        return pool_.alloc(std::move(t));
    }
    const LogosType* make_generic_datatype(std::string_view name,
                                   std::vector<const LogosType*> args,
                                   std::vector<std::string> lt_args = {},
                                   std::string_view pkg = {}) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::ZonedStruct;
        t.struct_name   = std::string(name);
        t.type_args     = std::move(args);
        t.lifetime_args = std::move(lt_args);
        if (!pkg.empty()) t.pkg_name = std::string(pkg);
        return pool_.alloc(std::move(t));
    }
    const LogosType* make_generic_struct(std::string_view name,
                                 std::vector<const LogosType*> args,
                                 std::vector<std::string> lt_args = {},
                                 std::string_view pkg = {}) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::Struct;
        t.struct_name   = std::string(name);
        t.type_args     = std::move(args);
        t.lifetime_args = std::move(lt_args);
        if (!pkg.empty()) t.pkg_name = std::string(pkg);
        return pool_.alloc(std::move(t));
    }
    const LogosType* make_enum_type(std::string_view name, std::string_view pkg = {}) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::Enum; t.enum_name = name;
        if (!pkg.empty()) t.pkg_name = std::string(pkg);
        return pool_.alloc(std::move(t));
    }
    const LogosType* make_tuple_type(std::vector<const LogosType*> elems) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::Tuple;
        t.tuple_elems = std::move(elems);
        return pool_.alloc(std::move(t));
    }
    const LogosType* make_closure_type(std::vector<const LogosType*> params, const LogosType* ret) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::Closure;
        t.closure_params = std::move(params);
        t.closure_ret = ret;
        return pool_.alloc(std::move(t));
    }
    const LogosType* make_fn_ptr_type(std::vector<const LogosType*> params, const LogosType* ret) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::FnPtr;
        t.closure_params = std::move(params);
        t.closure_ret = ret ? ret : void_t();
        return pool_.alloc(std::move(t));
    }
    // Coerce a non-capturing closure to fn ptr when target type is FnPtr.
    // Returns true if coercion was applied (arg's type is changed to FnPtr).
    bool try_coerce_closure_to_fnptr(lir::LExprPtr& arg, const LogosType* expected) {
        TypeRef er(expected);
        if (!arg || !er || er.kind() != LogosType::Kind::FnPtr) return false;
        TypeRef at(arg->type);
        if (!at || at.kind() != LogosType::Kind::Closure) return false;
        auto* box = std::get_if<lir::EClosureBox>(&arg->kind);
        if (!box || !box->inner || !box->inner->captures.empty()) return false;
        if (at.closure_params().size() != er.closure_params().size()) return false;
        for (size_t i = 0; i < at.closure_params().size(); ++i)
            if (!types_compatible(at.closure_params()[i], er.closure_params()[i])) return false;
        box->inner->as_fn_ptr = true;
        arg->type = make_fn_ptr_type(at.closure_params(), at.closure_ret().raw());
        return true;
    }
    const LogosType* make_slice_type(const LogosType* elem) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::Slice;
        t.elem = elem;
        return pool_.alloc(std::move(t));
    }
    const LogosType* make_trait_object(std::string_view tname) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::TraitObject;
        t.trait_name = std::string(tname);
        return pool_.alloc(std::move(t));
    }
    const LogosType* make_typevar(std::string_view name) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::TypeVar;
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

    const std::vector<std::string>* filenames_    = nullptr;
    const std::vector<bool>*        from_binary_  = nullptr;
    std::string  file_;
    std::string  cur_package_;
    lir::LProgram* cur_prog_ = nullptr;  // set during lower_module_items, used by lower_generic_call
    bool         cur_from_binary_ = false;   // current file is from a binary module
    uint32_t     node_line_ = 0;

    // Per-file import scope (wildcard: `use foo.bar;` makes all pub symbols of foo.bar visible)
    struct ImportScope {
        std::vector<std::string> wildcard_packages;
    };
    ImportScope cur_imports_;

    // Qualified key: "pkg::name" or "name" if pkg empty
    static std::string sema_key(std::string_view pkg, std::string_view name) {
        if (pkg.empty()) return std::string(name);
        std::string r;
        r.reserve(pkg.size() + 2 + name.size());
        r.append(pkg); r.append("::"); r.append(name);
        return r;
    }

    uint32_t     tmp_var_count_ = 0;   // for generating unique internal names

    uint32_t get_line(hermes::TinyMapView node) noexcept {
        using namespace sema_detail;
        if (node.is_null()) return 0;
        AnyVal av = node.get(la::SRC_LINE.code);
        if (av.is_null() || !av.is_value()) return 0;
        return av.as_value<uint32_t>();
    }

    // ── meta @{} helpers ─────────────────────────────────────────

    lir::HermesValPtr               eval_static_hermes_lit(hermes::TinyMapView node);
    std::shared_ptr<lir::HermesVal> extract_meta_val(hermes::TinyMapView node);

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

    // Read the VALUE of an annotation as an integer.  Accepts two AST shapes:
    //   #[name = 123]           → VALUE: { CODE: LIT_INT, VALUE: "123" }
    //   #[name = Enum::Variant] → VALUE: { CODE: ENUM_LIT, NAME: "Enum", FIELD: "Variant" }
    // Caller must have verified ann.has_key(la::VALUE).
    uint64_t read_annotation_u64(hermes::TinyMapView ann) {
        using namespace sema_detail;
        auto vmap = map_of(ann.get(la::VALUE.code));
        int32_t vc = code_of(vmap);
        if (vc == la::LIT_INT) {
            auto sv = str_of(vmap.get(la::VALUE.code));
            return (uint64_t)parse_int_literal(sv);
        }
        if (vc == la::ENUM_LIT) {
            auto ename = std::string(str_of(vmap.get(la::NAME.code)));
            auto vname = std::string(str_of(vmap.get(la::FIELD.code)));
            auto [epkg, esi] = find_enum_by_name(ename);
            auto it = esi ? enums_.find(sema_key(epkg, ename)) : enums_.end();
            if (it == enums_.end()) it = enums_.find(ename);
            if (it == enums_.end()) {
                error(std::format("enum '{}' not found in annotation value", ename));
                return 0;
            }
            for (auto& var : it->second.variants) {
                if (var.name == vname) return (uint64_t)(int64_t)var.value;
            }
            error(std::format("enum '{}' has no variant '{}'", ename, vname));
            return 0;
        }
        error("annotation value must be an integer literal or enum variant");
        return 0;
    }

    // ── Scope management ─────────────────────────────────────────

    struct VarInfo { const LogosType* type; bool is_mut = false; };
    struct Frame {
        logos::compiler::StrMap<VarInfo> vars;  // O(1) lookup
        std::vector<std::string> var_order;              // declaration order
    };
    std::vector<Frame> scope_;

    std::set<std::string> moved_vars_;   // variables consumed by move
    std::set<std::string> copy_types_;   // types with impl Copy — never move-only

    int destruct_counter_ = 0;           // unique-name source for `let (...)` temps

    // Phase 5: fns annotated `#[metaprogram_post_sema]`, gathered during
    // collect_module; copied into LProgram at the end of sema_lower.
    std::vector<std::string> metaprog_post_sema_hooks_;

    // Phase 7 slice 12: derive-style handler registry, collected from
    // `#[metaprog_handler("name")]` annotations on hook fns.
    std::vector<lir::LProgram::MetaprogHandler> metaprog_handlers_;
    std::vector<lir::LProgram::MetaprogTarget>  metaprog_targets_;

    // Phase 7 slice 17: metaprog-compile mode. When metaprog_mode_ is true,
    // lower_fn skips body lowering for non-handler fns in the entry ast
    // (cur_ast_idx_ == metaprog_entry_ast_idx_). Errors inside skipped
    // bodies don't reach result_.diags. Set via sema_lower's SemaOptions.
    bool   metaprog_mode_           = false;
    size_t metaprog_entry_ast_idx_  = static_cast<size_t>(-1);
    size_t cur_ast_idx_             = static_cast<size_t>(-1);

    bool fn_is_metaprog_handler(std::string_view name) const {
        for (const auto& mh : metaprog_handlers_)
            if (mh.hook_fn == name) return true;
        for (const auto& h : metaprog_post_sema_hooks_)
            if (h == name) return true;
        return false;
    }

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
    std::string_view struct_name_from_type(const LogosType* t);

    // ── Module-level symbol tables ───────────────────────────────

    struct SemaFieldInfo  { std::string_view name; const LogosType* type; bool is_pub = false; bool is_variadic = false; };
    struct SemaStructInfo { std::vector<SemaFieldInfo> fields; std::vector<TypeParam> type_params;
                            std::vector<std::string> lifetime_params;
                            bool is_pub = false; std::string source_file;
                            std::string package;
                            bool is_data_plain = true;  // false if any field is Kind::ZonedStruct
                            bool is_annotation_type = false;  // #[annotation] datatype (see LStructDef::is_annotation_type)
                            // Partial-spec support: when this is a specialization,
                            // base_name is the generic template (e.g. "Map") and
                            // spec_patterns holds one entry per type-param slot
                            // (either concrete types or TypeVars for partial specs).
                            std::string base_name;
                            std::vector<const LogosType*> spec_patterns;
                          };
    struct SemaFuncInfo   { std::vector<const LogosType*> param_types; const LogosType* ret_type;
                            std::vector<TypeParam> type_params; bool is_vararg = false;
                            bool is_pub = false; bool is_const = false; bool is_unsafe = false;
                            bool is_extern = false;
                            std::string base_name;
                            std::string signature_key;
                            std::string symbol_name;
                            std::string source_file; std::string package; };
    struct SemaVariantInfo{
        std::string_view name; int64_t value;
        std::vector<const LogosType*> payload_types;  // empty = no payload
        bool is_variadic = false;                     // variadic pack payload (...T)
    };
    struct SemaEnumInfo   {
        std::vector<SemaVariantInfo> variants;
        std::vector<TypeParam> type_params;  // for generic enums
        const LogosType* backing_type = nullptr;  // null = default (i32)
    };

    // ── Trait info ───────────────────────────────────────────────
    struct SemaTraitMethodInfo {
        std::string name;
        std::vector<TypeParam> type_params;  // method-level: `fn hash<H: Hasher>(...)` has [H]
        std::vector<const LogosType*> param_types;  // includes self
        const LogosType* ret_type = nullptr;
        bool has_default = false;   // trait method has a default body
        bool is_unsafe = false;     // declared unsafe fn in trait
        hermes::AnyVal default_ast{};    // AST node for default method (valid when has_default)
        hermes::MemHolder* default_holder = nullptr;  // zone that owns default_ast
    };
    struct SemaAssocTypeInfo {
        std::string name;              // e.g. "Item"
        std::vector<TraitBound> bounds;
        std::vector<TypeParam>  type_params;  // GAT params: type Item<T> has [T]
    };
    struct SemaAssocConstInfo {
        std::string      name;         // e.g. "MAX"
        const LogosType* type = nullptr;
    };
    struct SemaTraitInfo {
        std::string name;
        std::vector<TypeParam> type_params;  // e.g. trait Into<T> has T
        std::vector<SemaTraitMethodInfo> methods;
        std::vector<SemaAssocTypeInfo>   assoc_types;
        std::vector<SemaAssocConstInfo>  assoc_consts;
        std::vector<TraitBound> supertraits;  // e.g. [{Display,[]}, {Into,[i32]}] for trait Foo: Display + Into<i32>
        bool is_unsafe = false;               // declared as `unsafe trait`
        bool is_genos  = false;               // declared with `genos` keyword
        bool is_auto   = false;               // declared with `auto trait`
    };
    struct SemaImplInfo {
        std::string trait_name;
        std::string target_type;
        bool        is_unsafe = false;        // declared as `unsafe impl`
    };

    // Type params in scope for the function/struct currently being processed.
    // Maps type param name → TypeVar LogosType*.
    logos::compiler::StrMap<const LogosType*> current_type_params_;
    // Set by collect_impl/lower_impl_block for `impl<T>` blocks so that
    // collect_fn/lower_fn include the impl-level type params in the function.
    std::vector<TypeParam> impl_type_params_;

    // Re-export graph: pkg_reexports_["a.b"] = ["x.y", "z"] means `pub use x.y; pub use z;`
    // is declared in package a.b. Used by find_* helpers for transitive import resolution.
    logos::compiler::StrMap<std::vector<std::string>> pkg_reexports_;

    logos::compiler::StrMap<SemaStructInfo>   structs_;
    logos::compiler::StrMap<SemaStructInfo>   datatypes_;  // Hermes datatypes
    // Explicit type_code from #[type_code=N] annotations; populated in lower_module_items.
    logos::compiler::StrMap<uint64_t>         explicit_type_codes_;
    // concrete_name (e.g. "Pair__i32") → SemaStructInfo for explicit specializations.
    logos::compiler::StrMap<SemaStructInfo>   struct_specs_sema_;
    logos::compiler::StrMap<SemaEnumInfo>     enums_;
    logos::compiler::StrMap<SemaFuncInfo>     funcs_;
    // base name -> concrete overload symbols stored in funcs_.
    logos::compiler::StrMap<std::vector<std::string>> func_overloads_;
    // symbol_name -> generic function info.
    logos::compiler::StrMap<SemaFuncInfo>     generic_funcs_;
    // base name -> generic overload symbols stored in generic_funcs_.
    logos::compiler::StrMap<std::vector<std::string>> generic_overloads_;
    // const fn bodies: mangled_name → (body_block, param_names)
    struct ConstFnBody { hermes::TinyMapView body; std::vector<std::string> param_names; };
    logos::compiler::StrMap<ConstFnBody>      const_fn_bodies_;
    // Generic type alias entry: non-generic aliases have type_params empty.
    struct TypeAliasEntry {
        const LogosType*         type;
        std::vector<TypeParam>   type_params;
        std::vector<std::string> lifetime_params;  // e.g. ["'z"] for type Foo<'z, T> = ...
    };

    // Lifetime substitution map: "'z" → "'a"  (name → name, erased at codegen).
    using SemaLifetimeSubst = logos::compiler::StrMap<std::string>;
    logos::compiler::StrMap<TypeAliasEntry> type_aliases_;
    logos::compiler::StrMap<const LogosType*> module_consts_;
    logos::compiler::StrMap<SemaTraitInfo>    traits_;
    // "TraitName::TypeName" → impl info
    logos::compiler::StrMap<SemaImplInfo>     impls_;
    // "TraitName::TypeName::AssocName" → assoc type + type params for substitution.
    struct AssocTypeEntry {
        const LogosType*       type;
        std::vector<TypeParam> impl_type_params;  // from enclosing impl<T>
        std::vector<TypeParam> gat_type_params;   // from GAT itself: type Item<T> = ...
    };
    logos::compiler::StrMap<AssocTypeEntry> assoc_type_impls_;

    // "TraitName::TypeName::ConstName" → assoc const type (value evaluated lazily at call site)
    struct AssocConstEntry {
        const LogosType*  type;
        hermes::AnyVal    value_ast;  // AST expr node for the constant value
    };
    logos::compiler::StrMap<AssocConstEntry> assoc_const_impls_;

    // Current trait being defined (set during collect_trait for Self::Item resolution)
    std::string current_trait_name_;
    // Bounds per type param name (set alongside current_type_params_ during push_type_params)
    logos::compiler::StrMap<std::vector<TraitBound>> current_type_bounds_;

    // Blanket impls: `impl<T: Bound> Trait for T { fn method(…) … }`.  Stored
    // here during collect; consulted at method-call sites when direct lookup
    // on the concrete receiver type fails.  Each entry records one method of
    // one blanket impl.  mangled_name is "T__method" — the generic function
    // lives in generic_funcs_ under this name.
    struct BlanketImpl {
        std::string trait_name;       // e.g. "Datatype"
        std::string target_typevar;   // e.g. "T"
        std::string bound_trait;      // e.g. "Primitive"
        std::string method_name;      // e.g. "storage_new"
        std::string mangled_name;     // target_typevar + "__" + method_name
    };
    std::vector<BlanketImpl> blanket_impls_;

    // ── Package-qualified symbol lookup helpers ───────────────────

    // Look up struct/datatype/enum by LogosType (uses pkg_name if set, else unqualified fallback)
    SemaStructInfo* get_struct_si(TypeRef tr) {
        if (!tr) return nullptr;
        if (!tr.pkg_name().empty()) {
            auto it = structs_.find(sema_key(tr.pkg_name(), tr.struct_name()));
            if (it != structs_.end()) return &it->second;
            return nullptr;
        }
        auto it = structs_.find(tr.struct_name());
        return it != structs_.end() ? &it->second : nullptr;
    }
    SemaStructInfo* get_datatype_si(TypeRef tr) {
        if (!tr) return nullptr;
        if (!tr.pkg_name().empty()) {
            auto it = datatypes_.find(sema_key(tr.pkg_name(), tr.struct_name()));
            if (it != datatypes_.end()) return &it->second;
            return nullptr;
        }
        auto it = datatypes_.find(tr.struct_name());
        return it != datatypes_.end() ? &it->second : nullptr;
    }
    SemaEnumInfo* get_enum_si(TypeRef tr) {
        if (!tr) return nullptr;
        if (!tr.pkg_name().empty()) {
            auto it = enums_.find(sema_key(tr.pkg_name(), tr.enum_name()));
            if (it != enums_.end()) return &it->second;
            return nullptr;
        }
        auto it = enums_.find(tr.enum_name());
        return it != enums_.end() ? &it->second : nullptr;
    }

    // Collect all packages reachable from `pkg` via pub use re-exports (transitive, cycle-safe).
    void collect_reexports(const std::string& pkg,
                           StrSet& visited,
                           std::vector<std::string>& out) const {
        auto it = pkg_reexports_.find(pkg);
        if (it == pkg_reexports_.end()) return;
        for (auto& reexp : it->second) {
            if (visited.insert(reexp).second) {
                out.push_back(reexp);
                collect_reexports(reexp, visited, out);
            }
        }
    }

    // Build the full set of packages to search when resolving a name in context of cur_imports_.
    // Includes directly imported packages AND their transitive pub-use re-exports.
    std::vector<std::string> effective_import_pkgs() const {
        std::vector<std::string> result;
        StrSet visited;
        for (auto& pkg : cur_imports_.wildcard_packages) {
            if (visited.insert(pkg).second) {
                result.push_back(pkg);
                collect_reexports(pkg, visited, result);
            }
        }
        return result;
    }

    // Find struct by user-written name (searches cur_package_ then imports+reexports then unqualified)
    std::pair<std::string, SemaStructInfo*> find_struct_by_name(std::string_view name) {
        if (!cur_package_.empty()) {
            auto it = structs_.find(sema_key(cur_package_, std::string(name)));
            if (it != structs_.end()) return {cur_package_, &it->second};
        }
        for (auto& pkg : effective_import_pkgs()) {
            auto it = structs_.find(sema_key(pkg, std::string(name)));
            if (it != structs_.end()) {
                check_pub_access(it->second.is_pub, it->second.package, name);
                return {pkg, &it->second};
            }
        }
        auto it = structs_.find(std::string(name));
        if (it != structs_.end()) return {"", &it->second};
        return {"", nullptr};
    }
    std::pair<std::string, SemaStructInfo*> find_datatype_by_name(std::string_view name) {
        if (!cur_package_.empty()) {
            auto it = datatypes_.find(sema_key(cur_package_, std::string(name)));
            if (it != datatypes_.end()) return {cur_package_, &it->second};
        }
        for (auto& pkg : effective_import_pkgs()) {
            auto it = datatypes_.find(sema_key(pkg, std::string(name)));
            if (it != datatypes_.end()) {
                check_pub_access(it->second.is_pub, it->second.package, name);
                return {pkg, &it->second};
            }
        }
        auto it = datatypes_.find(std::string(name));
        if (it != datatypes_.end()) return {"", &it->second};
        return {"", nullptr};
    }
    std::pair<std::string, SemaEnumInfo*> find_enum_by_name(std::string_view name) {
        if (!cur_package_.empty()) {
            auto it = enums_.find(sema_key(cur_package_, std::string(name)));
            if (it != enums_.end()) return {cur_package_, &it->second};
        }
        for (auto& pkg : effective_import_pkgs()) {
            auto it = enums_.find(sema_key(pkg, std::string(name)));
            if (it != enums_.end()) return {pkg, &it->second};
        }
        auto it = enums_.find(std::string(name));
        if (it != enums_.end()) return {"", &it->second};
        return {"", nullptr};
    }
    std::pair<std::string, SemaTraitInfo*> find_trait_by_name(std::string_view name) {
        if (!cur_package_.empty()) {
            auto it = traits_.find(sema_key(cur_package_, std::string(name)));
            if (it != traits_.end()) return {cur_package_, &it->second};
        }
        for (auto& pkg : effective_import_pkgs()) {
            auto it = traits_.find(sema_key(pkg, std::string(name)));
            if (it != traits_.end()) return {pkg, &it->second};
        }
        auto it = traits_.find(std::string(name));
        if (it != traits_.end()) return {"", &it->second};
        return {"", nullptr};
    }
    bool has_struct(std::string_view name)   { return find_struct_by_name(name).second   != nullptr; }
    bool has_datatype(std::string_view name) { return find_datatype_by_name(name).second != nullptr; }
    bool has_enum(std::string_view name)     { return find_enum_by_name(name).second     != nullptr; }

    // ── Type parameter helpers ────────────────────────────────────

    std::vector<std::string> read_lifetime_params(hermes::TinyMapView node);
    std::vector<TypeParam> read_type_params_from(hermes::TinyMapView node, int32_t field_code);
    std::vector<TypeParam> read_type_params(hermes::TinyMapView node);

    // Save-and-restore stack so shadowing (e.g. trait<T> + method<T>) doesn't
    // wipe the outer binding on pop. Each push records the old value (if any)
    // keyed by call site (vector address); pop restores in reverse order.
    struct ShadowFrame {
        std::string name;
        const LogosType* old_type = nullptr;
        bool had_type = false;
        std::vector<TraitBound> old_bounds;
        bool had_bounds = false;
    };
    std::vector<std::vector<ShadowFrame>> type_param_shadow_stack_;

    void push_type_params(const std::vector<TypeParam>& tps) {
        type_param_shadow_stack_.emplace_back();
        auto& frames = type_param_shadow_stack_.back();
        frames.reserve(tps.size());
        for (auto& tp : tps) {
            ShadowFrame f;
            f.name = tp.name;
            auto it = current_type_params_.find(tp.name);
            if (it != current_type_params_.end()) { f.had_type = true; f.old_type = it->second; }
            auto bit = current_type_bounds_.find(tp.name);
            if (bit != current_type_bounds_.end()) { f.had_bounds = true; f.old_bounds = bit->second; }
            frames.push_back(std::move(f));
            if (tp.is_const) {
                LogosTypeBuilder c; c.kind = LogosType::Kind::ConstVar;
                c.type_var_name = tp.name;
                c.pointee = tp.const_type;
                current_type_params_[tp.name] = pool_.alloc(std::move(c));
            } else {
                current_type_params_[tp.name] = make_typevar(tp.name);
            }
            if (!tp.bounds.empty()) {
                current_type_bounds_[tp.name] = tp.bounds;
            } else {
                current_type_bounds_.erase(tp.name);
            }
        }
    }
    void pop_type_params(const std::vector<TypeParam>& /*tps*/) {
        if (type_param_shadow_stack_.empty()) return;
        auto& frames = type_param_shadow_stack_.back();
        for (auto rit = frames.rbegin(); rit != frames.rend(); ++rit) {
            if (rit->had_type) current_type_params_[rit->name] = rit->old_type;
            else               current_type_params_.erase(rit->name);
            if (rit->had_bounds) current_type_bounds_[rit->name] = rit->old_bounds;
            else                 current_type_bounds_.erase(rit->name);
        }
        type_param_shadow_stack_.pop_back();
    }

    // ── Sema-side type substitution (TypeVar → concrete) ────────────

    using SemaSubst = logos::compiler::StrMap<const LogosType*>;

    const LogosType* subst_type_sema(TypeRef t, const SemaSubst& s,
                                      const SemaLifetimeSubst& ls = {});

    // ── Compatibility ────────────────────────────────────────────
    bool compat(const LogosType* from, const LogosType* to) const {
        return types_compatible(from, to);
    }

    // ── Type resolution ──────────────────────────────────────────

    const LogosType* resolve_type(hermes::TinyMapView node);

    // ── Collection phase ─────────────────────────────────────────

    void collect(const std::vector<hermes::Hermes>& asts);
    void simplify_all_types();
    void check_supertrait_impls();
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
    void collect_struct(hermes::TinyMapView node);
    void collect_datatype(hermes::TinyMapView node, bool is_annotation_type = false);
    const LogosType* try_resolve_as_known_type(std::string_view name);
    bool is_known_type_name(std::string_view name) const;
    void extract_typevars_from_type_node(hermes::TinyMapView node,
                                         std::vector<TypeParam>& out);
    bool is_specialization_fn(hermes::TinyMapView node);
    bool is_specialization_struct(hermes::TinyMapView node);
    lir::LStructDef lower_spec_struct(hermes::TinyMapView node);
    lir::LFunction lower_spec_fn(hermes::TinyMapView node);
    void collect_fn(hermes::TinyMapView node, std::string_view struct_ctx = {});

    // ── Auto trait satisfaction ───────────────────────────────────

    // Recursively checks whether type T satisfies auto trait `trait_name`
    // (e.g. "Send" or "Sync"). Returns true if satisfied.
    bool is_auto_trait_satisfied(TypeRef tv, std::string_view trait_name,
                                  StrSet& visited);

    // Set by is_auto_trait_satisfied when it finds a non-satisfying field.
    struct AutoTraitOffender {
        std::string      field_name;
        const LogosType* field_ty = nullptr;
    };
    AutoTraitOffender last_offender_;

    // ── Loop depth / return type ─────────────────────────────────

    int loop_depth_ = 0;
    bool inside_unsafe_ = false;
    const LogosType* ret_type_ = nullptr;
    const LogosType* break_value_type_ = nullptr;  // type yielded by break <expr>
    bool break_without_value_ = false;
    std::string pending_loop_label_;  // set by LABELED_LOOP before lowering inner loop
    bool match_in_tail_position_ = false;
    const LogosType* impl_ret_type_inferred_ = nullptr;
    const LogosType* hint_enum_type_ = nullptr;
    const LogosType* hint_struct_type_ = nullptr;

    // ── Return reachability ───────────────────────────────────────

    bool stmt_always_returns(hermes::TinyMapView stmt);
    bool block_always_returns(hermes::TinyMapView block);
    // Like *_always_returns, but also treats `break`/`continue` as diverging.
    // Used where we need to know "does the tail expression run?" — e.g. match
    // arm body: `{ ...; break; }` never reaches a tail expression.
    bool stmt_always_diverts(hermes::TinyMapView stmt);
    bool block_always_diverts(hermes::TinyMapView block);

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

    const LogosType* field_type_of(std::string_view sname, std::string_view fname,
                                    std::string_view pkg_hint = {});
    const LogosType* field_type_of_for_type(const LogosType* struct_t, std::string_view fname);
    const SemaStructInfo* find_best_sema_struct_spec(std::string_view base_name,
                                                     const std::vector<const LogosType*>& type_args);
    std::string canonical_func_type_name(const LogosType* t) const;
    std::string function_signature_key(std::string_view base_name,
                                       const std::vector<const LogosType*>& param_types,
                                       bool is_vararg) const;
    std::string function_symbol_name(std::string_view base_name,
                                     const SemaFuncInfo& info) const;
    const SemaFuncInfo* find_func_by_symbol(std::string_view symbol) const;
    const SemaFuncInfo* find_generic_func(std::string_view base_name) const;
    const SemaFuncInfo* find_generic_func(std::string_view base_name,
                                          size_t n_args) const;
    const SemaFuncInfo* find_func_by_base_and_signature(std::string_view base_name,
                                                        const std::vector<const LogosType*>& param_types,
                                                        bool is_vararg = false) const;
    std::vector<const SemaFuncInfo*> find_func_candidates(std::string_view base_name) const;
    const SemaFuncInfo* resolve_function_call(std::string_view base_name,
                                              const std::vector<lir::LExprPtr>& arg_exprs,
                                              bool allow_generic = true,
                                              bool exact_only = false) const;

    // ── lower_expr ───────────────────────────────────────────────

    lir::LExprPtr lower_expr(hermes::TinyMapView expr);
    lir::LExprPtr lower_binop(hermes::TinyMapView node);
    lir::LExprPtr lower_unary(hermes::TinyMapView node);
    lir::LExprPtr lower_deref(hermes::TinyMapView node);
    lir::LExprPtr lower_call(hermes::TinyMapView node);
    void unify_types(TypeRef formal, TypeRef actual,
                     logos::compiler::StrMap<const LogosType*>& bindings);
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
    lir::LExprPtr lower_method_call(hermes::TinyMapView node);
    lir::LExprPtr lower_field_read(hermes::TinyMapView node);
    lir::LExprPtr lower_struct_lit(hermes::TinyMapView node);
    lir::LExprPtr lower_index_read(hermes::TinyMapView node);
    lir::LExprPtr lower_arr_lit(hermes::TinyMapView node);
    lir::LExprPtr lower_arr_fill_lit(hermes::TinyMapView node);
    lir::LExprPtr lower_list_comp(hermes::TinyMapView node);
    lir::LExprPtr lower_map_comp(hermes::TinyMapView node);
    lir::LExprPtr lower_hermes_list_comp(hermes::TinyMapView node);
    lir::LExprPtr lower_hermes_map_comp(hermes::TinyMapView node);
    lir::LExprPtr coerce_to_hermes_anyval(lir::LExprPtr val,
                                          const std::string& ctr_var,
                                          const LogosType* ctr_t,
                                          std::string_view context);
    lir::LExprPtr lower_hermes_lit(hermes::TinyMapView node);

    // Capture context: non-null while lowering a hermes literal that has $-captures.
    // lower_hermes_val populates it as it encounters HERMES_CAP_IDENT/EXPR nodes.
    struct HermesCapCtx {
        std::vector<lir::LExprPtr>               exprs;       // unique capture expressions
        std::vector<const LogosType*>             types;       // corresponding types
        uint32_t                                  next_slot = 0; // next param_index
        // dedup: symbol binding name → value_index (for pure EIdent captures)
        logos::compiler::StrMap<uint32_t> ident_dedup;
    };
    HermesCapCtx* hermes_cap_ctx_ = nullptr;

    lir::HermesValPtr lower_hermes_val(hermes::TinyMapView node);
    lir::LExprPtr lower_enum_lit(hermes::TinyMapView node);
    lir::LExprPtr lower_enum_lit_data(hermes::TinyMapView node);
    lir::LExprPtr lower_enum_lit_data_from_static(
            hermes::TinyMapView node, std::string_view ename, std::string_view vname);
    lir::LExprPtr lower_static_call(hermes::TinyMapView node);
    lir::LExprPtr lower_if_expr(hermes::TinyMapView node);
    lir::LExprPtr lower_closure_expr(hermes::TinyMapView node);

    // ── lower_stmt and friends ───────────────────────────────────

    lir::LStmt lower_stmt(hermes::TinyMapView stmt);
    lir::LBlock lower_block(hermes::TinyMapView block);
    lir::LStmt lower_let_destruct(hermes::TinyMapView node);
    lir::LStmt lower_let(hermes::TinyMapView node);
    lir::LStmt lower_let_else(hermes::TinyMapView node);
    lir::LStmt lower_compound_assign(hermes::TinyMapView node);
    lir::LStmt lower_assign(hermes::TinyMapView node);
    lir::LStmt lower_return(hermes::TinyMapView node);
    lir::Pattern build_pattern(hermes::TinyMapView pnode, const LogosType* scrut_type);
    // If pnode is a Hermes scalar pattern (PAT_HERMES_NULL/BOOL/INT), returns a
    // bool-typed guard call that evaluates the pattern against `scrut_var`
    // (which must be an AnyVal).  Returns nullptr otherwise.
    struct HermesPatBinding {
        std::string name;        // user-visible binding name in arm body
        std::string av_var;      // AnyVal local holding the value
    };
    lir::LExprPtr build_hermes_pat_guard(hermes::TinyMapView pnode,
                                         const std::string& scrut_var,
                                         const LogosType* scrut_type,
                                         const std::string& base_var,
                                         std::vector<lir::LStmt>& out_stmts,
                                         std::vector<HermesPatBinding>& out_bindings);
    // Returns the "inner" (ref-stripped) view type if `t` is Hermes,
    // HermesView<'_>, or HermesStatic (possibly behind &/&mut). nullptr otherwise.
    const LogosType* hermes_view_inner(const LogosType* t) const {
        TypeRef tr(t);
        if (!tr) return nullptr;
        TypeRef inner = tr;
        if (tr.kind() == LogosType::Kind::Ref ||
            tr.kind() == LogosType::Kind::MutRef)
            inner = tr.pointee();
        if (!inner) return nullptr;
        if ((inner.kind() == LogosType::Kind::Struct ||
             inner.kind() == LogosType::Kind::ZonedStruct) &&
            (inner.struct_name() == "Hermes" ||
             inner.struct_name() == "HermesView" ||
             inner.struct_name() == "HermesStatic"))
            return inner.raw();
        return nullptr;
    }
    // True while lowering match arms where Hermes scalar patterns are
    // explicitly handled by the caller (desugared to guard). Outside this
    // context, PAT_HERMES_* in build_pattern is a diagnostic.
    bool in_match_hermes_ctx_ = false;
    void bind_pattern(const lir::Pattern& pat,
                      const LogosType* scrut_type = nullptr);
    lir::LStmt lower_if(hermes::TinyMapView node);
    lir::LStmt lower_while(hermes::TinyMapView node);
    lir::LStmt lower_for(hermes::TinyMapView node);
    lir::LStmt lower_for_each(hermes::TinyMapView node);
    lir::LStmt lower_loop(hermes::TinyMapView node);
    lir::LStmt lower_field_write(hermes::TinyMapView node);
    lir::LStmt lower_chain_field_write(hermes::TinyMapView node);
    lir::LStmt lower_chain_field_compound_assign(hermes::TinyMapView node);
    lir::LStmt lower_field_compound_assign(hermes::TinyMapView node);
    lir::LStmt lower_tuple_field_write(hermes::TinyMapView node);
    lir::LStmt lower_tuple_field_compound_assign(hermes::TinyMapView node);
    lir::LStmt lower_deref_field_write(hermes::TinyMapView node);
    lir::LStmt lower_deref_field_compound_assign(hermes::TinyMapView node);
    lir::LStmt lower_index_write(hermes::TinyMapView node);
    lir::LStmt lower_index_compound_assign(hermes::TinyMapView node);
    lir::LStmt lower_field_index_write(hermes::TinyMapView node);
    lir::LStmt lower_field_index_compound_assign(hermes::TinyMapView node);
    lir::LStmt lower_match(hermes::TinyMapView node);
    lir::LExprPtr lower_match_expr(hermes::TinyMapView node);

    // ── lower_fn and declaration lowering ───────────────────────

    lir::LFunction lower_fn(hermes::TinyMapView node, std::string_view struct_ctx = {});
    lir::LStructDef lower_struct_def(hermes::TinyMapView node);
    lir::LEnumDef lower_enum_def(hermes::TinyMapView node);
    using ConstEnv = logos::compiler::StrMap<int64_t>;
    std::optional<int64_t> const_eval_expr(hermes::TinyMapView e, const ConstEnv& env);
    std::optional<int64_t> const_eval_block(hermes::TinyMapView block, ConstEnv env);
    lir::LExprPtr try_const_fold_call(hermes::TinyMapView call_node);
    lir::LConst lower_const_def(hermes::TinyMapView node);
    lir::LTypeAlias lower_type_alias_def(hermes::TinyMapView node);
    lir::LTraitDef lower_trait_def(hermes::TinyMapView node);
    void lower_impl_block(hermes::TinyMapView node, lir::LProgram& prog);
    void lower_program(const std::vector<hermes::Hermes>& asts, lir::LProgram& prog);
    void lower_module_items(hermes::TinyMapView mod, lir::LProgram& prog);
    lir::LAnnotationValue parse_annot_literal(hermes::TinyMapView v);
    std::optional<lir::LAnnotationInstance>
        build_annotation_instance(hermes::TinyMapView ann,
                                  std::string_view ann_name,
                                  std::string_view ann_pkg,
                                  const SemaStructInfo& ann_info);
};

// ── File-scope helpers used across sema_*.cpp TUs ─────────────────────────
// These are declared/defined inline so each TU gets a copy without ODR
// violations.  They were previously file-static in the anonymous namespace.

inline bool is_integer_kind(LogosType::Kind k) noexcept {
    return k == LogosType::Kind::I32   || k == LogosType::Kind::I64 ||
           k == LogosType::Kind::U8    || k == LogosType::Kind::I8  ||
           k == LogosType::Kind::I16   || k == LogosType::Kind::U16 ||
           k == LogosType::Kind::U32   || k == LogosType::Kind::U64 ||
           k == LogosType::Kind::I24   || k == LogosType::Kind::U24 ||
           k == LogosType::Kind::I24   || k == LogosType::Kind::U56 ||
           k == LogosType::Kind::I56   || k == LogosType::Kind::U24 ||
           k == LogosType::Kind::I56   || k == LogosType::Kind::U56 ||
           k == LogosType::Kind::I128  || k == LogosType::Kind::U128 ||
           k == LogosType::Kind::IntLit || k == LogosType::Kind::Enum;
}

// Bit-width and signedness of a concrete integer kind.  Returns {0,false}
// for non-concrete-integer kinds (IntLit, Enum, non-integers).
inline std::pair<unsigned, bool> int_rank(LogosType::Kind k) noexcept {
    using K = LogosType::Kind;
    switch (k) {
    case K::I8:   return {8, true};
    case K::U8:   return {8, false};
    case K::I16:  return {16, true};
    case K::U16:  return {16, false};
    case K::I24:  return {24, true};
    case K::U24:  return {24, false};
    case K::I32:  return {32, true};
    case K::U32:  return {32, false};
    case K::I56:  return {56, true};
    case K::U56:  return {56, false};
    case K::I64:  return {64, true};
    case K::U64:  return {64, false};
    case K::I128: return {128, true};
    case K::U128: return {128, false};
    default:      return {0, false};
    }
}

// True iff every value of `from` is representable in `to` — i.e. a safe
// implicit widening:
//   signed   → signed   : to_width ≥ from_width
//   unsigned → unsigned : to_width ≥ from_width
//   unsigned → signed   : to_width > from_width (need one extra bit)
//   signed   → unsigned : never (loses negative values)
inline bool can_widen_int(LogosType::Kind from, LogosType::Kind to) noexcept {
    auto [fw, fs] = int_rank(from);
    auto [tw, ts] = int_rank(to);
    if (fw == 0 || tw == 0) return false;
    if (fs == ts) return tw >= fw;
    if (!fs && ts) return tw > fw;
    return false;
}

inline const LogosType* unify_int(const LogosType* a, const LogosType* b) noexcept {
    if (a->kind == LogosType::Kind::IntLit) return b;
    if (b->kind == LogosType::Kind::IntLit) return a;
    // Widen narrower to wider when safe: caller has already verified compat.
    if (can_widen_int(a->kind, b->kind)) return b;
    if (can_widen_int(b->kind, a->kind)) return a;
    return a;
}

// If `e`'s type is a concrete integer kind strictly narrower than `target`,
// and widens safely, wrap `e` in ECast(target).  No-op for IntLit (literals
// are retyped directly, not cast) and for types that don't safely widen.
inline void widen_int_expr(lir::LExprPtr& e, const LogosType* target) {
    if (!e || !target || !e->type) return;
    if (e->type->kind == target->kind) return;
    if (!can_widen_int(e->type->kind, target->kind)) return;
    auto inner = std::move(e);
    e = std::make_unique<lir::LExpr>();
    e->kind = lir::ECast{std::move(inner)};
    e->type = target;
}

// Like unify_int but also promotes FloatLit to a concrete float type (F32/F64).
// Use in contexts where both integers and floats need unification.
inline const LogosType* unify_numeric(const LogosType* a, const LogosType* b) noexcept {
    if (a->kind == LogosType::Kind::IntLit || a->kind == LogosType::Kind::FloatLit) return b;
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
    case LogosType::Kind::I16:  return v >= -32768 && v <= 32767;
    case LogosType::Kind::U16:  return v >= 0 && v <= 65535;
    case LogosType::Kind::I32:  return v >= (int64_t)INT32_MIN && v <= (int64_t)INT32_MAX;
    case LogosType::Kind::U32:  return v >= 0 && (uint64_t)v <= (uint64_t)UINT32_MAX;
    case LogosType::Kind::I24:  return v >= -(INT64_C(1) << 23) && v <= ((INT64_C(1) << 23) - 1);
    case LogosType::Kind::I56:  return v >= -(INT64_C(1) << 55) && v <= ((INT64_C(1) << 55) - 1);
    case LogosType::Kind::U24:  return v >= 0 && (uint64_t)v <= ((UINT64_C(1) << 24) - 1);
    case LogosType::Kind::U56:  return v >= 0 && (uint64_t)v <= ((UINT64_C(1) << 56) - 1);
    case LogosType::Kind::I128: return true; // all int64_t values fit in i128
    case LogosType::Kind::U128: return v >= 0; // non-negative int64_t values fit
    default: return true; // i64, u64: all int64_t values fit
    }
}

inline int64_t parse_int_literal(std::string_view sv) noexcept {
    bool negative = !sv.empty() && sv[0] == '-';
    if (negative) sv = sv.substr(1);
    // NG3: use uint64_t to avoid signed overflow during accumulation.
    // Parsing "9223372036854775808" (INT64_MAX+1) into int64_t would be UB.
    uint64_t result = 0;
    if (sv.size() >= 2 && sv[0] == '0') {
        if (sv[1] == 'x' || sv[1] == 'X') {
            for (char c : sv.substr(2)) {
                if (c == '_') continue;
                if (c >= '0' && c <= '9')      result = result * 16 + (uint64_t)(c - '0');
                else if (c >= 'a' && c <= 'f') result = result * 16 + (uint64_t)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') result = result * 16 + (uint64_t)(c - 'A' + 10);
                else break;
            }
        } else if (sv[1] == 'b' || sv[1] == 'B') {
            for (char c : sv.substr(2)) {
                if (c == '_') continue;
                if (c == '0' || c == '1') result = result * 2 + (uint64_t)(c - '0');
                else break;
            }
        } else if (sv[1] == 'o' || sv[1] == 'O') {
            for (char ch : sv.substr(2)) {
                if (ch == '_') continue;
                if (ch >= '0' && ch <= '7') result = result * 8 + (uint64_t)(ch - '0');
                else break;
            }
        } else {
            for (char c : sv) {
                if (c == '_') continue;
                if (c >= '0' && c <= '9') result = result * 10 + (uint64_t)(c - '0');
                else break;
            }
        }
    } else {
        for (char c : sv) {
            if (c == '_') continue;
            if (c >= '0' && c <= '9') result = result * 10 + (uint64_t)(c - '0');
            else break;
        }
    }
    if (negative) {
        // Special case: -(2^63) = INT64_MIN, which overflows if cast to int64 first.
        if (result == (uint64_t)INT64_MAX + 1) return INT64_MIN;
        if (result > (uint64_t)INT64_MAX)      return INT64_MIN;  // saturate
        return -(int64_t)result;
    }
    // Raw bit-cast: preserve full 64-bit pattern for u64 literals whose value
    // exceeds INT64_MAX (e.g. FNV offset basis 0xcbf29ce484222325).
    return (int64_t)result;
}

template <class Pred>
inline bool valid_digit_groups(std::string_view sv, Pred pred) noexcept {
    if (sv.empty()) return false;
    bool prev_us = false;
    for (size_t i = 0; i < sv.size(); ++i) {
        char c = sv[i];
        if (c == '_') {
            if (i == 0 || i + 1 == sv.size() || prev_us) return false;
            prev_us = true;
            continue;
        }
        if (!pred(c)) return false;
        prev_us = false;
    }
    return true;
}

inline bool valid_int_literal_format(std::string_view sv) noexcept {
    if (sv.empty()) return false;
    size_t start = (sv.front() == '-') ? 1 : 0;
    if (start >= sv.size()) return false;

    static constexpr std::string_view suffixes[] = {
        "usize", "isize",
        "i128", "i64", "i56", "i32", "i24", "i16", "i8",
        "u128", "u64", "u56", "u32", "u24", "u16", "u8"
    };
    size_t suffix_len = 0;
    for (auto sfx : suffixes) {
        if (sv.size() >= start + sfx.size() && sv.substr(sv.size() - sfx.size()) == sfx) {
            suffix_len = sfx.size();
            break;
        }
    }

    size_t body_end = sv.size() - suffix_len;
    size_t body_start = start;
    auto is_dec = [](char c) { return c >= '0' && c <= '9'; };
    auto is_hex = [](char c) {
        return (c >= '0' && c <= '9') ||
               (c >= 'a' && c <= 'f') ||
               (c >= 'A' && c <= 'F');
    };
    auto is_bin = [](char c) { return c == '0' || c == '1'; };
    auto is_oct = [](char c) { return c >= '0' && c <= '7'; };

    if (body_end >= body_start + 2 && sv[body_start] == '0') {
        char p = sv[body_start + 1];
        if (p == 'x' || p == 'X') {
            body_start += 2;
            return valid_digit_groups(sv.substr(body_start, body_end - body_start), is_hex);
        }
        if (p == 'b' || p == 'B') {
            body_start += 2;
            return valid_digit_groups(sv.substr(body_start, body_end - body_start), is_bin);
        }
        if (p == 'o' || p == 'O') {
            body_start += 2;
            return valid_digit_groups(sv.substr(body_start, body_end - body_start), is_oct);
        }
    }
    return valid_digit_groups(sv.substr(body_start, body_end - body_start), is_dec);
}

inline bool valid_float_literal_format(std::string_view sv) noexcept {
    if (sv.empty()) return false;
    size_t start = (sv.front() == '-') ? 1 : 0;
    if (start >= sv.size()) return false;

    size_t suffix_len = 0;
    if (sv.size() >= start + 3 &&
        (sv.substr(sv.size() - 3) == "f32" || sv.substr(sv.size() - 3) == "f64"))
        suffix_len = 3;

    std::string_view main = sv.substr(start, sv.size() - start - suffix_len);
    size_t dot = main.find('.');
    if (dot == std::string_view::npos) return false;

    auto is_dec = [](char c) { return c >= '0' && c <= '9'; };
    if (!valid_digit_groups(main.substr(0, dot), is_dec)) return false;

    size_t exp = main.find_first_of("eE", dot + 1);
    std::string_view frac = (exp == std::string_view::npos)
        ? main.substr(dot + 1)
        : main.substr(dot + 1, exp - dot - 1);
    if (!valid_digit_groups(frac, is_dec)) return false;

    if (exp != std::string_view::npos) {
        size_t exp_start = exp + 1;
        if (exp_start < main.size() && (main[exp_start] == '+' || main[exp_start] == '-'))
            ++exp_start;
        if (!valid_digit_groups(main.substr(exp_start), is_dec)) return false;
    }
    return true;
}

// Returns the LogosType::Kind for a numeric suffix like "i32", "u64", "f32".
// Returns Kind::Error if sv has no recognised suffix.
inline LogosType::Kind int_suffix_kind(std::string_view sv) noexcept {
    // Strip leading sign and numeric digits / underscores to find the suffix.
    size_t i = 0;
    if (i < sv.size() && sv[i] == '-') ++i;
    // Skip hex/bin/oct prefix
    if (i + 1 < sv.size() && sv[i] == '0' &&
        (sv[i+1] == 'x' || sv[i+1] == 'X' ||
         sv[i+1] == 'b' || sv[i+1] == 'B' ||
         sv[i+1] == 'o' || sv[i+1] == 'O')) i += 2;
    // Skip digit body
    while (i < sv.size() && (std::isxdigit((unsigned char)sv[i]) || sv[i] == '_')) ++i;
    auto suf = sv.substr(i);
    if (suf == "i8")    return LogosType::Kind::I8;
    if (suf == "i16")   return LogosType::Kind::I16;
    if (suf == "i32")   return LogosType::Kind::I32;
    if (suf == "i24")   return LogosType::Kind::I24;
    if (suf == "i56")   return LogosType::Kind::I56;
    if (suf == "i64")   return LogosType::Kind::I64;
    if (suf == "i128")  return LogosType::Kind::I128;
    if (suf == "u8")    return LogosType::Kind::U8;
    if (suf == "u16")   return LogosType::Kind::U16;
    if (suf == "u32")   return LogosType::Kind::U32;
    if (suf == "u24")   return LogosType::Kind::U24;
    if (suf == "u56")   return LogosType::Kind::U56;
    if (suf == "u64")   return LogosType::Kind::U64;
    if (suf == "u128")  return LogosType::Kind::U128;
    if (suf == "usize") return LogosType::Kind::U64;  // treat usize as u64
    if (suf == "isize") return LogosType::Kind::I64;  // treat isize as i64
    return LogosType::Kind::Error;
}

inline LogosType::Kind float_suffix_kind(std::string_view sv) noexcept {
    if (sv.size() >= 3 && sv.substr(sv.size() - 3) == "f32") return LogosType::Kind::F32;
    if (sv.size() >= 3 && sv.substr(sv.size() - 3) == "f64") return LogosType::Kind::F64;
    return LogosType::Kind::Error;
}

} // namespace logos::compiler
