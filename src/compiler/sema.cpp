// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/compiler/sema.hpp>
#include <logos/compiler/ast.hpp>
#include <logos/hermes/view.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/any_val.hpp>

#include <deque>
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
        return a.struct_name == b.struct_name;
    default:
        return true;   // primitive kinds equal by kind alone
    }
}

// Structural assignment/argument compatibility.
// Extends types_equal with:
//   [T; N] → *T  and  [T; N] → *mut T   (array-to-pointer coercion)
static bool is_integer_kind(LogosType::Kind k) noexcept {
    return k == LogosType::Kind::I32   || k == LogosType::Kind::I64 ||
           k == LogosType::Kind::U8    || k == LogosType::Kind::I8  ||
           k == LogosType::Kind::U32   || k == LogosType::Kind::U64 ||
           k == LogosType::Kind::IntLit;
}

static bool types_compatible(const LogosType* from, const LogosType* to) noexcept {
    if (!from || !to) return false;
    if (types_equal(*from, *to)) return true;
    // IntLit widens to any concrete integer type.
    if (from->kind == LogosType::Kind::IntLit && is_integer_kind(to->kind))
        return true;
    // Array → pointer coercion: [T; N] is compatible with *const T or *mut T
    if (from->kind == LogosType::Kind::Array &&
        to->kind   == LogosType::Kind::Ptr   &&
        from->elem && to->pointee)
        return types_equal(*from->elem, *to->pointee);
    return false;
}

// Return the concrete integer type when one operand is an unresolved literal.
// Precondition: both are integer kinds.
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
        return std::string(t->mut_ptr ? "*mut " : "*const ") +
               type_str(t->pointee);
    case LogosType::Kind::Array:
        return std::format("[{}; {}]", type_str(t->elem), t->arr_size);
    case LogosType::Kind::Struct:
        return std::string(t->struct_name);
    case LogosType::Kind::Error:  return "<error>";
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

// ── TypePool ──────────────────────────────────────────────────────────────
// Owns all LogosType objects.  std::deque provides pointer-stable push_back.

class TypePool {
    std::deque<LogosType> pool_;
public:
    const LogosType* alloc(LogosType t) {
        pool_.push_back(std::move(t));
        return &pool_.back();
    }
};

// ── SemaChecker ───────────────────────────────────────────────────────────

class SemaChecker {
public:
    SemaResult run(const std::vector<hermes::HermesCtr>& asts,
                   const std::vector<std::string>& filenames) {
        filenames_ = &filenames;
        init_primitives();
        collect(asts);
        if (!result_.ok()) return result_;  // stop if collection failed
        check(asts);
        return result_;
    }

private:
    // ── Type pool and primitives ─────────────────────────────────

    TypePool pool_;

    // Indexed by Kind ordinal.  Kind goes 0..13 (Void..Error), so size 14.
    // Only primitive kinds (Void, I32..U64, IntLit, Error) are populated;
    // compound kinds (Ptr=9, Array=10, Struct=11) are created via make_* helpers.
    std::array<const LogosType*, 14> prims_{};

    void init_primitives() {
        auto alloc_prim = [&](LogosType::Kind k) {
            LogosType t;
            t.kind = k;
            prims_[int(k)] = pool_.alloc(t);
        };
        alloc_prim(LogosType::Kind::Void);
        alloc_prim(LogosType::Kind::I32);
        alloc_prim(LogosType::Kind::I64);
        alloc_prim(LogosType::Kind::F64);
        alloc_prim(LogosType::Kind::Bool);
        alloc_prim(LogosType::Kind::U8);
        alloc_prim(LogosType::Kind::I8);
        alloc_prim(LogosType::Kind::U32);
        alloc_prim(LogosType::Kind::U64);
        alloc_prim(LogosType::Kind::IntLit);
        alloc_prim(LogosType::Kind::Error);
    }

    const LogosType* prim(LogosType::Kind k)   { return prims_[int(k)]; }
    const LogosType* void_t()    { return prim(LogosType::Kind::Void); }
    const LogosType* i32_t()     { return prim(LogosType::Kind::I32); }
    const LogosType* bool_t()    { return prim(LogosType::Kind::Bool); }
    const LogosType* u8_t()      { return prim(LogosType::Kind::U8); }
    const LogosType* intlit_t()  { return prim(LogosType::Kind::IntLit); }
    const LogosType* error_t()   { return prim(LogosType::Kind::Error); }

    const LogosType* make_ptr(bool mut, const LogosType* pointee) {
        LogosType t;
        t.kind    = LogosType::Kind::Ptr;
        t.mut_ptr = mut;
        t.pointee = pointee;
        return pool_.alloc(t);
    }

    const LogosType* make_array(const LogosType* elem, uint64_t n) {
        LogosType t;
        t.kind     = LogosType::Kind::Array;
        t.elem     = elem;
        t.arr_size = n;
        return pool_.alloc(t);
    }

    const LogosType* make_struct_type(std::string_view name) {
        LogosType t;
        t.kind        = LogosType::Kind::Struct;
        t.struct_name = name;
        return pool_.alloc(t);
    }

    // ── File / line tracking ─────────────────────────────────────

    const std::vector<std::string>* filenames_ = nullptr;
    std::string  file_;       // current module's source file
    uint32_t     node_line_ = 0;  // line of the node currently being checked

    uint32_t get_line(TinyMapView node) noexcept {
        if (node.is_null()) return 0;
        AnyVal av = node.get(la::SRC_LINE.code);
        if (av.is_null() || !av.is_value()) return 0;
        return av.as_value<uint32_t>();
    }

    // ── Hermes navigation ────────────────────────────────────────

    MemHolder* holder_ = nullptr;

    int32_t code_of(TinyMapView node) noexcept {
        if (node.is_null()) return -1;
        AnyVal av = node.get(la::CODE.code);  // unchecked uint8_t overload
        return av.is_null() ? -1 : av.as_value<int32_t>();
    }

    std::string_view str_of(AnyVal av) noexcept {
        if (av.is_null()) return {};
        return StringView(av.to_offset(), holder_).view();
    }

    // Returns a null TinyMapView if av is null — prevents asserting on empty nodes.
    TinyMapView map_of(AnyVal av) noexcept {
        if (av.is_null()) return TinyMapView{};
        return TinyMapView(av.to_offset(), holder_);
    }

    ArrayView arr_of(AnyVal av) noexcept {
        return ArrayView(av.to_offset(), holder_);
    }

    // ── Diagnostics ──────────────────────────────────────────────

    SemaResult result_;
    std::string ctx_;   // current context string for error messages

    void error(std::string msg) {
        result_.diags.push_back({Diag::Level::Error, ctx_, std::move(msg), file_, node_line_});
    }

    void warn(std::string msg) {
        result_.diags.push_back({Diag::Level::Warning, ctx_, std::move(msg), file_, node_line_});
    }

    // ── Scope management ─────────────────────────────────────────

    struct VarInfo {
        const LogosType* type;
        bool is_mut = false;
    };
    struct Frame {
        std::unordered_map<std::string, VarInfo> vars;
    };
    std::vector<Frame> scope_;

    void push_scope() { scope_.emplace_back(); }

    void pop_scope() {
        if (!scope_.empty()) scope_.pop_back();
    }

    void define(std::string_view name, const LogosType* t, bool is_mut = false) {
        if (!scope_.empty())
            scope_.back().vars[std::string(name)] = {t, is_mut};
    }

    // Returns nullptr if not found.
    const LogosType* lookup(std::string_view name) const {
        for (auto it = scope_.rbegin(); it != scope_.rend(); ++it) {
            auto f = it->vars.find(std::string(name));
            if (f != it->vars.end()) return f->second.type;
        }
        return nullptr;
    }

    // Returns false if not found or not mutable.
    bool lookup_is_mut(std::string_view name) const {
        for (auto it = scope_.rbegin(); it != scope_.rend(); ++it) {
            auto f = it->vars.find(std::string(name));
            if (f != it->vars.end()) return f->second.is_mut;
        }
        return false;
    }

    // For field / method access: unwrap *Struct → Struct, return struct_name.
    // Returns empty if var is not a struct or pointer-to-struct.
    std::string_view struct_name_of(std::string_view var_name) {
        auto* t = lookup(var_name);
        if (!t) return {};
        if (t->kind == LogosType::Kind::Struct) return t->struct_name;
        if (t->kind == LogosType::Kind::Ptr &&
            t->pointee &&
            t->pointee->kind == LogosType::Kind::Struct)
            return t->pointee->struct_name;
        return {};
    }

    // ── Module-level symbol tables ───────────────────────────────

    struct SemaFieldInfo {
        std::string_view name;       // view into Hermes arena
        const LogosType* type;
    };
    struct SemaStructInfo {
        std::vector<SemaFieldInfo> fields;
    };
    struct SemaFuncInfo {
        std::vector<const LogosType*> param_types;
        const LogosType* ret_type;
    };

    std::unordered_map<std::string, SemaStructInfo> structs_;
    std::unordered_map<std::string, SemaFuncInfo>   funcs_;

    // ── Type resolution ──────────────────────────────────────────

    const LogosType* resolve_type(TinyMapView node) {
        int32_t tc = code_of(node);

        if (tc == la::PTR_TYPE) {
            bool mut = false;
            AnyVal mut_av = node.get(la::MUTPTR.code);
            if (!mut_av.is_null() && mut_av.is_value())
                mut = mut_av.as_value<uint8_t>() != 0;
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
            // User-defined struct
            if (structs_.count(std::string(name)))
                return make_struct_type(name);
            error(std::format("unknown type '{}'", name));
            return error_t();
        }

        error(std::format("unexpected type node code {}", tc));
        return error_t();
    }

    // ── Collection phase ─────────────────────────────────────────
    // Builds structs_ and funcs_ from all modules.  Must run before check().

    void collect(const std::vector<hermes::HermesCtr>& asts) {
        // First pass: collect all struct names (so struct fields can reference
        // structs defined later in the same or another module).
        for (auto& ast : asts) {
            holder_ = ast.holder();
            auto root = ast.root_object().as_tiny_map();
            if (!root.has_key(la::ITEMS)) continue;
            auto items = arr_of(root.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto item = map_of(items.get(i));
                if (code_of(item) == la::STRUCT) {
                    auto sname = std::string(str_of(item.get(la::NAME.code)));
                    if (structs_.count(sname))
                        error(std::format("duplicate struct '{}'", sname));
                    else
                        structs_[sname] = {};  // placeholder
                }
            }
        }

        // Second pass: collect struct fields and all function signatures.
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
            if (c == la::STRUCT)     collect_struct(item);
            else if (c == la::FN || c == la::EXTERN_FN)
                collect_fn(item);
        }
    }

    void collect_struct(TinyMapView node) {
        auto sname = std::string(str_of(node.get(la::NAME.code)));
        ctx_ = std::format("struct {}", sname);

        SemaStructInfo info;

        // Collect field definitions.
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

        // Collect methods as mangled free functions.
        if (node.has_key(la::ITEMS)) {
            auto methods = arr_of(node.get(la::ITEMS.code));
            for (uint64_t m = 0; m < methods.size(); ++m) {
                auto method = map_of(methods.get(m));
                if (code_of(method) == la::FN)
                    collect_fn(method, sname);
            }
        }
    }

    void collect_fn(TinyMapView node, std::string_view struct_ctx = {}) {
        auto raw_name = str_of(node.get(la::NAME.code));
        std::string mangled = struct_ctx.empty()
            ? std::string(raw_name)
            : std::string(struct_ctx) + "__" + std::string(raw_name);

        ctx_ = std::format("fn {}", mangled);

        if (funcs_.count(mangled)) {
            error(std::format("duplicate function '{}'", mangled));
            return;
        }

        SemaFuncInfo info;

        // Parameters.
        if (node.has_key(la::PARAMS)) {
            auto params_av = node.get(la::PARAMS.code);
            if (params_av.is_pointer()) {
                auto params_node = map_of(params_av);
                // PARAM_LIST node has ITEMS array, or it's a single PARAM.
                // The grammar wraps params in a node with ITEMS: $...
                if (params_node.has_key(la::ITEMS)) {
                    auto arr = arr_of(params_node.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < arr.size(); ++i) {
                        auto p = map_of(arr.get(i));
                        if (code_of(p) != la::PARAM) continue;
                        info.param_types.push_back(
                            resolve_type(map_of(p.get(la::TYPE.code))));
                    }
                }
            }
        }

        // Return type.
        if (node.has_key(la::RET_TYPE)) {
            info.ret_type = resolve_type(map_of(node.get(la::RET_TYPE.code)));
        } else {
            info.ret_type = void_t();
        }

        funcs_[mangled] = std::move(info);
    }

    // ── Loop depth tracking ──────────────────────────────────────
    int loop_depth_ = 0;

    // ── Current function state ────────────────────────────────────

    const LogosType* ret_type_ = nullptr;  // expected return type

    // ── Check phase ──────────────────────────────────────────────

    void check(const std::vector<hermes::HermesCtr>& asts) {
        for (size_t i = 0; i < asts.size(); ++i) {
            holder_ = asts[i].holder();
            file_ = (filenames_ && i < filenames_->size()) ? (*filenames_)[i] : std::string{};
            auto root = asts[i].root_object().as_tiny_map();
            check_module(root);
        }
    }

    void check_module(TinyMapView mod) {
        if (!mod.has_key(la::ITEMS)) return;
        auto items = arr_of(mod.get(la::ITEMS.code));
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto item = map_of(items.get(i));
            int32_t c = code_of(item);
            if (c == la::FN)        check_fn(item);
            else if (c == la::STRUCT) {
                // Check methods inside structs.
                auto sname = str_of(item.get(la::NAME.code));
                if (item.has_key(la::ITEMS)) {
                    auto methods = arr_of(item.get(la::ITEMS.code));
                    for (uint64_t m = 0; m < methods.size(); ++m) {
                        auto method = map_of(methods.get(m));
                        if (code_of(method) == la::FN)
                            check_fn(method, sname);
                    }
                }
            }
            // EXTERN_FN: no body to check.
        }
    }

    // ── Return-reachability analysis ─────────────────────────────
    // Returns true if every execution path through the node reaches a `return`.

    bool stmt_always_returns(TinyMapView stmt) {
        int32_t c = code_of(stmt);
        if (c == la::RETURN) return true;
        // loop {} always returns if its body always returns (i.e. no break).
        if (c == la::LOOP) {
            return stmt.has_key(la::BODY) &&
                   block_always_returns(map_of(stmt.get(la::BODY.code)));
        }
        if (c == la::IF) {
            // Only guarantees a return if BOTH branches always return.
            if (!stmt.has_key(la::ELSE)) return false;
            bool then_ret = stmt.has_key(la::THEN) &&
                            block_always_returns(map_of(stmt.get(la::THEN.code)));
            auto else_node = map_of(stmt.get(la::ELSE.code));
            bool else_ret  = (code_of(else_node) == la::BLOCK)
                             ? block_always_returns(else_node)
                             : stmt_always_returns(else_node);   // else-if
            return then_ret && else_ret;
        }
        // while, let, assign, expr_stmt, field_write, index_write — no guarantee.
        return false;
    }

    bool block_always_returns(TinyMapView block) {
        if (!block.has_key(la::ITEMS)) return false;
        auto stmts = arr_of(block.get(la::ITEMS.code));
        for (uint64_t i = 0; i < stmts.size(); ++i) {
            auto stmt = map_of(stmts.get(i));
            if (!stmt.is_null() && stmt_always_returns(stmt))
                return true;   // rest of block is unreachable
        }
        return false;
    }

    void check_fn(TinyMapView node, std::string_view struct_ctx = {}) {
        auto raw_name = str_of(node.get(la::NAME.code));
        std::string mangled = struct_ctx.empty()
            ? std::string(raw_name)
            : std::string(struct_ctx) + "__" + std::string(raw_name);

        ctx_ = std::format("fn {}", mangled);
        node_line_ = get_line(node);

        auto fit = funcs_.find(mangled);
        if (fit == funcs_.end()) return;  // shouldn't happen after collect

        ret_type_ = fit->second.ret_type;

        // Build initial scope from parameters.
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
                        auto ptype = fit->second.param_types[i];
                        define(pname, ptype);
                    }
                }
            }
        }

        // Check body.
        if (node.has_key(la::BODY)) {
            auto body = map_of(node.get(la::BODY.code));
            check_block(body);

            // Return-reachability: every path must return for non-void functions.
            if (ret_type_ && ret_type_->kind != LogosType::Kind::Void &&
                ret_type_->kind != LogosType::Kind::Error &&
                !block_always_returns(body)) {
                error("not all paths return a value");
            }
        }

        pop_scope();
    }

    void check_block(TinyMapView block) {
        push_scope();
        if (block.has_key(la::ITEMS)) {
            auto stmts = arr_of(block.get(la::ITEMS.code));
            for (uint64_t i = 0; i < stmts.size(); ++i) {
                auto stmt = map_of(stmts.get(i));
                if (!stmt.is_null()) check_stmt(stmt);
            }
        }
        pop_scope();
    }

    void check_stmt(TinyMapView stmt) {
        node_line_ = get_line(stmt);
        int32_t c = code_of(stmt);

        if (c == la::LET) {
            check_let(stmt);
        } else if (c == la::ASSIGN) {
            check_assign(stmt);
        } else if (c == la::RETURN) {
            check_return(stmt);
        } else if (c == la::IF) {
            check_if(stmt);
        } else if (c == la::WHILE) {
            check_while(stmt);
        } else if (c == la::EXPR_STMT) {
            if (stmt.has_key(la::VALUE))
                check_expr(map_of(stmt.get(la::VALUE.code)));
        } else if (c == la::FIELD_WRITE) {
            check_field_write(stmt);
        } else if (c == la::INDEX_WRITE) {
            check_index_write(stmt);
        } else if (c == la::LOOP) {
            if (stmt.has_key(la::BODY)) {
                ++loop_depth_;
                check_block(map_of(stmt.get(la::BODY.code)));
                --loop_depth_;
            }
        } else if (c == la::BREAK || c == la::CONTINUE) {
            if (loop_depth_ == 0)
                error(c == la::BREAK ? "'break' outside loop" : "'continue' outside loop");
        } else {
            // Unknown statement kind — silently skip.
        }
    }

    void check_let(TinyMapView node) {
        auto name = str_of(node.get(la::NAME.code));

        bool is_mut = false;
        if (node.has_key(la::IS_MUT)) {
            AnyVal av = node.get(la::IS_MUT.code);
            if (!av.is_null() && av.is_value())
                is_mut = av.as_value<uint8_t>() != 0;
        }

        // Check RHS expression.
        const LogosType* rhs_type = nullptr;
        if (node.has_key(la::VALUE)) {
            rhs_type = check_expr(map_of(node.get(la::VALUE.code)));
        } else {
            error(std::format("let '{}': missing value", name));
            rhs_type = error_t();
        }

        // If explicit type annotation, check compatibility.
        if (node.has_key(la::TYPE)) {
            auto* ann = resolve_type(map_of(node.get(la::TYPE.code)));
            if (ann->kind != LogosType::Kind::Error &&
                rhs_type->kind != LogosType::Kind::Error &&
                !types_compatible(rhs_type, ann)) {
                error(std::format("let '{}': type mismatch — expected {}, got {}",
                      name, type_str(ann), type_str(rhs_type)));
            }
            define(name, ann, is_mut);
        } else {
            define(name, rhs_type, is_mut);
        }
    }

    void check_assign(TinyMapView node) {
        auto name = str_of(node.get(la::NAME.code));
        auto* var_type = lookup(name);
        if (!var_type) {
            error(std::format("assignment to undefined variable '{}'", name));
            if (node.has_key(la::VALUE))
                check_expr(map_of(node.get(la::VALUE.code)));
            return;
        }
        if (!lookup_is_mut(name)) {
            error(std::format("assignment to immutable variable '{}'", name));
        }
        if (node.has_key(la::VALUE)) {
            auto* rhs = check_expr(map_of(node.get(la::VALUE.code)));
            if (var_type->kind != LogosType::Kind::Error &&
                rhs->kind     != LogosType::Kind::Error &&
                !types_compatible(rhs, var_type)) {
                error(std::format("assignment to '{}': type mismatch — expected {}, got {}",
                      name, type_str(var_type), type_str(rhs)));
            }
        }
    }

    void check_return(TinyMapView node) {
        if (node.has_key(la::VALUE)) {
            auto val_av = node.get(la::VALUE.code);
            if (!val_av.is_null()) {
                auto* t = check_expr(map_of(val_av));
                if (ret_type_ && ret_type_->kind != LogosType::Kind::Error &&
                    t->kind != LogosType::Kind::Error &&
                    !types_compatible(t, ret_type_)) {
                    error(std::format("return type mismatch — expected {}, got {}",
                          type_str(ret_type_), type_str(t)));
                }
                return;
            }
        }
        // return; with no value — must match void
        if (ret_type_ && ret_type_->kind != LogosType::Kind::Void &&
            ret_type_->kind != LogosType::Kind::Error) {
            error(std::format("return without value in function returning {}",
                  type_str(ret_type_)));
        }
    }

    void check_if(TinyMapView node) {
        if (node.has_key(la::COND)) {
            auto* ct = check_expr(map_of(node.get(la::COND.code)));
            if (ct->kind != LogosType::Kind::Bool &&
                ct->kind != LogosType::Kind::Error) {
                error(std::format("if condition must be bool, got {}",
                      type_str(ct)));
            }
        }
        if (node.has_key(la::THEN))
            check_block(map_of(node.get(la::THEN.code)));
        if (node.has_key(la::ELSE)) {
            auto else_node = map_of(node.get(la::ELSE.code));
            if (code_of(else_node) == la::BLOCK)
                check_block(else_node);
            else
                check_stmt(else_node);  // else-if
        }
    }

    void check_while(TinyMapView node) {
        if (node.has_key(la::COND)) {
            auto* ct = check_expr(map_of(node.get(la::COND.code)));
            if (ct->kind != LogosType::Kind::Bool &&
                ct->kind != LogosType::Kind::Error) {
                error(std::format("while condition must be bool, got {}",
                      type_str(ct)));
            }
        }
        if (node.has_key(la::BODY)) {
            ++loop_depth_;
            check_block(map_of(node.get(la::BODY.code)));
            --loop_depth_;
        }
    }

    void check_field_write(TinyMapView node) {
        auto recv_name  = str_of(node.get(la::RECEIVER.code));
        auto field_name = str_of(node.get(la::FIELD.code));
        auto sname = struct_name_of(recv_name);
        if (sname.empty()) {
            error(std::format("field write: '{}' is not a struct", recv_name));
            if (node.has_key(la::VALUE))
                check_expr(map_of(node.get(la::VALUE.code)));
            return;
        }
        // Mutability check: immutable struct variable or *const ptr.
        auto* recv_type = lookup(recv_name);
        if (recv_type && recv_type->kind == LogosType::Kind::Ptr) {
            if (!recv_type->mut_ptr)
                error(std::format("field write to '{}': receiver is *const pointer",
                      recv_name));
        } else if (!lookup_is_mut(recv_name)) {
            error(std::format("field write to immutable variable '{}'", recv_name));
        }
        auto* field_type = field_type_of(sname, field_name);
        if (!field_type) {
            error(std::format("field write: struct '{}' has no field '{}'",
                  sname, field_name));
        }
        if (node.has_key(la::VALUE)) {
            auto* vt = check_expr(map_of(node.get(la::VALUE.code)));
            if (field_type && field_type->kind != LogosType::Kind::Error &&
                vt->kind != LogosType::Kind::Error &&
                !types_compatible(vt, field_type)) {
                error(std::format("field write '{}.{}': expected {}, got {}",
                      recv_name, field_name, type_str(field_type), type_str(vt)));
            }
        }
    }

    void check_index_write(TinyMapView node) {
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

        // Check index is integer.
        if (node.has_key(la::LHS)) {
            auto* it = check_expr(map_of(node.get(la::LHS.code)));
            if (!is_integer(it)) {
                error(std::format("array index must be an integer, got {}", type_str(it)));
            }
        }

        // Determine element type: array → elem, pointer → pointee.
        const LogosType* elem_type = nullptr;
        if (arr_type) {
            if (arr_type->kind == LogosType::Kind::Array)
                elem_type = arr_type->elem;
            else if (arr_type->kind == LogosType::Kind::Ptr)
                elem_type = arr_type->pointee;
        }

        // Check value type matches element type.
        if (node.has_key(la::VALUE) && elem_type) {
            auto* vt = check_expr(map_of(node.get(la::VALUE.code)));
            if (elem_type->kind != LogosType::Kind::Error &&
                vt->kind != LogosType::Kind::Error &&
                !types_compatible(vt, elem_type)) {
                error(std::format("index write to '{}': expected {}, got {}",
                      arr_name, type_str(elem_type), type_str(vt)));
            }
        } else if (node.has_key(la::VALUE)) {
            check_expr(map_of(node.get(la::VALUE.code)));
        }
    }

    // ── Expression type checking ─────────────────────────────────
    // Returns error_t() on failure (never nullptr).

    const LogosType* check_expr(TinyMapView expr) {
        if (expr.is_null()) return error_t();
        node_line_ = get_line(expr);
        int32_t c = code_of(expr);

        switch (c) {
        case la::LIT_INT:   return intlit_t();
        case la::LIT_BOOL:  return bool_t();
        case la::LIT_STR:   return make_ptr(false, u8_t());  // *const u8

        case la::VAR_REF: {
            auto name = str_of(expr.get(la::NAME.code));
            auto* t = lookup(name);
            if (!t) {
                error(std::format("undefined variable '{}'", name));
                return error_t();
            }
            return t;
        }

        case la::PAREN_EXPR:
            if (expr.has_key(la::VALUE))
                return check_expr(map_of(expr.get(la::VALUE.code)));
            return error_t();

        case la::CAST: {
            if (expr.has_key(la::VALUE))
                check_expr(map_of(expr.get(la::VALUE.code)));
            if (expr.has_key(la::TYPE))
                return resolve_type(map_of(expr.get(la::TYPE.code)));
            return error_t();
        }
        case la::BINOP:   return check_binop(expr);
        case la::UNARY:   return check_unary(expr);
        case la::DEREF:   return check_deref(expr);
        case la::CALL:    return check_call(expr);
        case la::METHOD_CALL: return check_method_call(expr);
        case la::FIELD_READ:  return check_field_read(expr);
        case la::STRUCT_LIT:  return check_struct_lit(expr);
        case la::INDEX_READ:  return check_index_read(expr);
        case la::ARR_LIT:     return check_arr_lit(expr);

        default:
            // Unknown expression — silently treat as error type.
            return error_t();
        }
    }

    const LogosType* check_binop(TinyMapView node) {
        auto op = str_of(node.get(la::OP.code));
        auto* lhs = check_expr(map_of(node.get(la::LHS.code)));
        auto* rhs = check_expr(map_of(node.get(la::RHS.code)));
        if (lhs->kind == LogosType::Kind::Error ||
            rhs->kind == LogosType::Kind::Error)
            return error_t();

        // Logical operators: bool × bool → bool
        if (op == "&&" || op == "||") {
            if (lhs->kind != LogosType::Kind::Bool)
                error(std::format("operator '{}': left operand must be bool, got {}", op, type_str(lhs)));
            if (rhs->kind != LogosType::Kind::Bool)
                error(std::format("operator '{}': right operand must be bool, got {}", op, type_str(rhs)));
            return bool_t();
        }

        // Comparison operators: T × T → bool
        // Integer literals are compatible with any integer type.
        if (op == "==" || op == "!=" ||
            op == "<"  || op == "<=" ||
            op == ">"  || op == ">=") {
            bool ok = types_compatible(lhs, rhs) || types_compatible(rhs, lhs);
            if (!ok)
                error(std::format("operator '{}': operand type mismatch ({} vs {})",
                      op, type_str(lhs), type_str(rhs)));
            return bool_t();
        }

        // Arithmetic: numeric × numeric → result type (unified if one is IntLit)
        if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%") {
            if (!is_numeric(lhs))
                error(std::format("operator '{}': left operand must be numeric, got {}", op, type_str(lhs)));
            if (!is_numeric(rhs))
                error(std::format("operator '{}': right operand must be numeric, got {}", op, type_str(rhs)));
            bool both_int = is_integer_kind(lhs->kind) && is_integer_kind(rhs->kind);
            if (!both_int) {
                // For f64: types must match exactly.
                if (is_numeric(lhs) && is_numeric(rhs) && !types_equal(*lhs, *rhs))
                    error(std::format("operator '{}': operand type mismatch ({} vs {})",
                          op, type_str(lhs), type_str(rhs)));
                return lhs;
            }
            // Integer operands: IntLit widens to the concrete side.
            if (!types_compatible(lhs, rhs) && !types_compatible(rhs, lhs))
                error(std::format("operator '{}': operand type mismatch ({} vs {})",
                      op, type_str(lhs), type_str(rhs)));
            return unify_int(lhs, rhs);
        }

        error(std::format("unknown binary operator '{}'", op));
        return error_t();
    }

    const LogosType* check_unary(TinyMapView node) {
        auto op  = str_of(node.get(la::OP.code));
        auto* vt = check_expr(map_of(node.get(la::VALUE.code)));
        if (vt->kind == LogosType::Kind::Error) return error_t();

        if (op == "-") {
            if (!is_numeric(vt))
                error(std::format("unary '-': operand must be numeric, got {}", type_str(vt)));
            return vt;
        }
        if (op == "!") {
            if (vt->kind != LogosType::Kind::Bool)
                error(std::format("unary '!': operand must be bool, got {}", type_str(vt)));
            return bool_t();
        }
        if (op == "&") {
            // Address-of: result is *const T
            return make_ptr(false, vt);
        }
        error(std::format("unknown unary operator '{}'", op));
        return error_t();
    }

    const LogosType* check_deref(TinyMapView node) {
        auto* vt = check_expr(map_of(node.get(la::VALUE.code)));
        if (vt->kind == LogosType::Kind::Error) return error_t();
        if (vt->kind != LogosType::Kind::Ptr) {
            error(std::format("dereference of non-pointer type {}", type_str(vt)));
            return error_t();
        }
        return vt->pointee ? vt->pointee : error_t();
    }

    const LogosType* check_call(TinyMapView node) {
        auto callee = str_of(node.get(la::CALLEE.code));
        auto fit = funcs_.find(std::string(callee));
        if (fit == funcs_.end()) {
            error(std::format("call to undefined function '{}'", callee));
            // Still type-check args to catch nested errors.
            if (node.has_key(la::ARGS)) {
                auto args = arr_of(node.get(la::ARGS.code));
                for (uint64_t i = 0; i < args.size(); ++i)
                    check_expr(map_of(args.get(i)));
            }
            return error_t();
        }

        auto& fi = fit->second;
        uint64_t n_args = 0;
        if (node.has_key(la::ARGS)) {
            auto args = arr_of(node.get(la::ARGS.code));
            n_args = args.size();
            if (n_args != fi.param_types.size()) {
                error(std::format("call to '{}': expected {} args, got {}",
                      callee, fi.param_types.size(), n_args));
            }
            for (uint64_t i = 0; i < args.size(); ++i) {
                auto* at = check_expr(map_of(args.get(i)));
                if (i < fi.param_types.size()) {
                    auto* pt = fi.param_types[i];
                    if (at->kind != LogosType::Kind::Error &&
                        pt->kind != LogosType::Kind::Error &&
                        !types_compatible(at, pt)) {
                        error(std::format("call to '{}' arg {}: expected {}, got {}",
                              callee, i + 1, type_str(pt), type_str(at)));
                    }
                }
            }
        } else if (!fi.param_types.empty()) {
            error(std::format("call to '{}': expected {} args, got 0",
                  callee, fi.param_types.size()));
        }

        return fi.ret_type;
    }

    const LogosType* check_method_call(TinyMapView node) {
        auto recv_name  = str_of(node.get(la::RECEIVER.code));
        auto method_name= str_of(node.get(la::NAME.code));
        auto sname = struct_name_of(recv_name);
        if (sname.empty()) {
            error(std::format("method call: '{}' is not a struct", recv_name));
            return error_t();
        }
        auto mangled = std::string(sname) + "__" + std::string(method_name);
        auto fit = funcs_.find(mangled);
        if (fit == funcs_.end()) {
            error(std::format("method call: '{}' has no method '{}'",
                  sname, method_name));
            return error_t();
        }

        auto& fi = fit->second;
        // First param is implicit 'self' — caller provides explicit args for the rest.
        uint64_t explicit_args = 0;
        if (node.has_key(la::ARGS))
            explicit_args = arr_of(node.get(la::ARGS.code)).size();

        size_t expected_explicit = fi.param_types.size() > 0
                                   ? fi.param_types.size() - 1 : 0;
        if (explicit_args != expected_explicit) {
            error(std::format("method call '{}': expected {} args, got {}",
                  mangled, expected_explicit, explicit_args));
        }

        // Check explicit arg types (params[1..]).
        if (node.has_key(la::ARGS)) {
            auto args = arr_of(node.get(la::ARGS.code));
            for (uint64_t i = 0; i < args.size(); ++i) {
                auto* at = check_expr(map_of(args.get(i)));
                size_t pi = i + 1;  // skip self param
                if (pi < fi.param_types.size()) {
                    auto* pt = fi.param_types[pi];
                    if (at->kind != LogosType::Kind::Error &&
                        pt->kind != LogosType::Kind::Error &&
                        !types_compatible(at, pt)) {
                        error(std::format("method '{}' arg {}: expected {}, got {}",
                              mangled, i + 1, type_str(pt), type_str(at)));
                    }
                }
            }
        }

        return fi.ret_type;
    }

    const LogosType* check_field_read(TinyMapView node) {
        auto recv_name  = str_of(node.get(la::RECEIVER.code));
        auto field_name = str_of(node.get(la::FIELD.code));
        auto sname = struct_name_of(recv_name);
        if (sname.empty()) {
            error(std::format("field read: '{}' is not a struct", recv_name));
            return error_t();
        }
        auto* ft = field_type_of(sname, field_name);
        if (!ft) {
            error(std::format("field read: struct '{}' has no field '{}'",
                  sname, field_name));
            return error_t();
        }
        return ft;
    }

    const LogosType* check_struct_lit(TinyMapView node) {
        auto sname = str_of(node.get(la::NAME.code));
        auto sit = structs_.find(std::string(sname));
        if (sit == structs_.end()) {
            error(std::format("struct literal: unknown struct '{}'", sname));
            return error_t();
        }
        auto& sinfo = sit->second;

        // Track which fields are initialized.
        std::unordered_map<std::string, bool> initialized;
        for (auto& f : sinfo.fields) initialized[std::string(f.name)] = false;

        if (node.has_key(la::ITEMS)) {
            auto inits = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < inits.size(); ++i) {
                auto init = map_of(inits.get(i));
                auto fname = str_of(init.get(la::NAME.code));

                auto it = initialized.find(std::string(fname));
                if (it == initialized.end()) {
                    error(std::format("struct literal '{}': unknown field '{}'",
                          sname, fname));
                } else {
                    it->second = true;
                }

                if (init.has_key(la::VALUE)) {
                    auto* vt = check_expr(map_of(init.get(la::VALUE.code)));
                    auto* ft = field_type_of(sname, fname);
                    if (ft && ft->kind != LogosType::Kind::Error &&
                        vt->kind != LogosType::Kind::Error &&
                        !types_compatible(vt, ft)) {
                        error(std::format("struct literal '{}' field '{}': expected {}, got {}",
                              sname, fname, type_str(ft), type_str(vt)));
                    }
                }
            }
        }

        // Check for uninitialized fields.
        for (auto& [fname, init] : initialized) {
            if (!init)
                error(std::format("struct literal '{}': field '{}' not initialized",
                      sname, fname));
        }

        return make_struct_type(sname);
    }

    const LogosType* check_index_read(TinyMapView node) {
        auto arr_name = str_of(node.get(la::NAME.code));
        auto* arr_type = lookup(arr_name);
        if (!arr_type) {
            error(std::format("index read: undefined variable '{}'", arr_name));
            if (node.has_key(la::VALUE))
                check_expr(map_of(node.get(la::VALUE.code)));
            return error_t();
        }
        if (arr_type->kind != LogosType::Kind::Array &&
            arr_type->kind != LogosType::Kind::Ptr &&
            arr_type->kind != LogosType::Kind::Error) {
            error(std::format("index read: '{}' is not an array or pointer (got {})",
                  arr_name, type_str(arr_type)));
        }
        if (node.has_key(la::VALUE)) {
            auto* it = check_expr(map_of(node.get(la::VALUE.code)));
            if (!is_integer(it))
                error(std::format("array index must be integer, got {}", type_str(it)));
        }
        if (arr_type->kind == LogosType::Kind::Array && arr_type->elem)
            return arr_type->elem;
        if (arr_type->kind == LogosType::Kind::Ptr && arr_type->pointee)
            return arr_type->pointee;
        return error_t();
    }

    const LogosType* check_arr_lit(TinyMapView node) {
        if (!node.has_key(la::ITEMS)) {
            // Empty array literal — type cannot be inferred.
            warn("empty array literal: element type unknown");
            return error_t();
        }
        auto items = arr_of(node.get(la::ITEMS.code));
        if (items.size() == 0) {
            warn("empty array literal: element type unknown");
            return error_t();
        }
        auto* elem_type = check_expr(map_of(items.get(0)));
        for (uint64_t i = 1; i < items.size(); ++i) {
            auto* t = check_expr(map_of(items.get(i)));
            if (t->kind != LogosType::Kind::Error &&
                elem_type->kind != LogosType::Kind::Error) {
                if (!types_compatible(t, elem_type) && !types_compatible(elem_type, t)) {
                    error(std::format("array literal: element {} has type {}, expected {}",
                          i, type_str(t), type_str(elem_type)));
                } else {
                    elem_type = unify_int(elem_type, t);
                }
            }
        }
        // Default unresolved integer literals to i32.
        if (elem_type->kind == LogosType::Kind::IntLit)
            elem_type = i32_t();
        return make_array(elem_type, items.size());
    }

    // ── Helpers ──────────────────────────────────────────────────

    static bool is_numeric(const LogosType* t) noexcept {
        if (!t) return false;
        return t->kind == LogosType::Kind::F64 || is_integer_kind(t->kind);
    }

    static bool is_integer(const LogosType* t) noexcept {
        return t && is_integer_kind(t->kind);
    }

    // Look up a field's type in a struct (returns nullptr if not found).
    const LogosType* field_type_of(std::string_view sname,
                                   std::string_view fname) {
        auto sit = structs_.find(std::string(sname));
        if (sit == structs_.end()) return nullptr;
        for (auto& f : sit->second.fields)
            if (f.name == fname) return f.type;
        return nullptr;
    }
};

} // anonymous namespace

SemaResult sema_check(const std::vector<logos::hermes::HermesCtr>& asts,
                      const std::vector<std::string>& filenames) {
    SemaChecker checker;
    return checker.run(asts, filenames);
}

} // namespace logos::compiler
