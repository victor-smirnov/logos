// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// MLIRGen — lower Logos Hermes AST to MLIR.
//
// Iteration 4: arrays ([T;N] type, [e,...] literal, arr[i] subscript).
// All let bindings use alloca so that reassignment and loop-carried state
// work correctly (LLVM mem2reg handles the cleanup).

#include "mlir_gen.hpp"

#include <logos/compiler/ast.hpp>
#include <logos/hermes/document.hpp>
#include <logos/hermes/view.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/any_val.hpp>

#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/BuiltinTypes.h>
#include <mlir/IR/MLIRContext.h>
#include <mlir/IR/Verifier.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/Dialect/ControlFlow/IR/ControlFlowOps.h>
#include <mlir/Dialect/LLVMIR/LLVMDialect.h>

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>

namespace logos::compiler {
namespace {

namespace la = logos::compiler::ast;
using hermes::TinyMapView;
using hermes::ArrayView;
using hermes::StringView;
using hermes::AnyVal;
using hermes::MemHolder;

// ---------------------------------------------------------------------------
// Struct type registry
// ---------------------------------------------------------------------------
struct FieldInfo {
    std::string  name;
    mlir::Type   type;
    uint32_t     index;
    std::string  struct_name; // non-empty if this field is a pointer to a struct
};

struct StructInfo {
    std::string                  name;
    mlir::LLVM::LLVMStructType   llvm_type;
    std::vector<FieldInfo>       fields;
};

// ---------------------------------------------------------------------------
// MLIRGenImpl — stateful AST walker
// ---------------------------------------------------------------------------
class MLIRGenImpl {
public:
    explicit MLIRGenImpl(mlir::MLIRContext& ctx)
        : builder_(&ctx)
        , loc_(builder_.getUnknownLoc())
        , holder_(nullptr)
    {}

    mlir::OwningOpRef<mlir::ModuleOp> generate(const std::vector<hermes::HermesCtr>& asts) {
        auto mod = mlir::ModuleOp::create(loc_);

        struct ModItems { hermes::MemHolder* holder; ArrayView items; };
        std::vector<ModItems> all_modules;

        for (auto& ast : asts) {
            auto h = ast.holder();
            holder_ = h;
            auto root_obj = ast.root_object();
            auto root = root_obj.as_tiny_map();
            int32_t code = code_of(root);
            if (code != la::MODULE) {
                std::fprintf(stderr, "mlir_gen: expected MODULE node, got code %d\n", code);
                return nullptr;
            }
            AnyVal items_av = root.get(la::ITEMS);
            if (items_av.is_null() || !items_av.is_pointer()) continue;
            all_modules.push_back({h, ArrayView(items_av.to_offset(), h)});
        }

        // Pass 0: register struct types.
        for (auto& [h, items] : all_modules) {
            holder_ = h;
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto item = map_of(items.get(i));
                if (code_of(item) == la::STRUCT)
                    if (!register_struct(item)) return nullptr;
            }
        }

        // Pass 0.5: collect type aliases and module-level constants.
        for (auto& [h, items] : all_modules) {
            holder_ = h;
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto item = map_of(items.get(i));
                int32_t ic = code_of(item);
                if (ic == la::TYPE_ALIAS) {
                    auto name = std::string(str_of(item.get(la::NAME)));
                    if (item.has_key(la::TYPE)) {
                        auto t = resolve_type(map_of(item.get(la::TYPE)));
                        if (t) type_aliases_[name] = t;
                    }
                } else if (ic == la::CONST_DEF) {
                    auto name = std::string(str_of(item.get(la::NAME)));
                    if (!item.has_key(la::VALUE)) continue;
                    mlir::Type ctype = builder_.getI32Type();
                    if (item.has_key(la::TYPE))
                        ctype = resolve_type(map_of(item.get(la::TYPE)));
                    module_consts_[name] = {h, item.get(la::VALUE), ctype};
                }
            }
        }

        // Pass 1: forward-declare all functions and methods.
        for (auto& [h, items] : all_modules) {
            holder_ = h;
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto item = map_of(items.get(i));
                int32_t item_code = code_of(item);

                if (item_code == la::EXTERN_FN) {
                    auto name = std::string(str_of(item.get(la::NAME)));
                    if (!mod.lookupSymbol<mlir::func::FuncOp>(name)) {
                        auto fn = gen_extern_fn(item);
                        if (!fn) return nullptr;
                        mod.push_back(fn);
                    }
                } else if (item_code == la::FN) {
                    auto name = std::string(str_of(item.get(la::NAME)));
                    if (!mod.lookupSymbol<mlir::func::FuncOp>(name)) {
                        auto decl = mlir::func::FuncOp::create(loc_, name, make_fn_type(item));
                        mod.push_back(decl);
                    }
                } else if (item_code == la::STRUCT) {
                    auto struct_name = std::string(str_of(item.get(la::NAME)));
                    if (!item.has_key(la::ITEMS)) continue;
                    auto methods_av = item.get(la::ITEMS);
                    if (methods_av.is_null() || !methods_av.is_pointer()) continue;
                    auto methods = arr_of(methods_av);
                    for (uint64_t m = 0; m < methods.size(); ++m) {
                        auto method = map_of(methods.get(m));
                        if (code_of(method) != la::FN) continue;
                        auto method_name = struct_name + "__" +
                                           std::string(str_of(method.get(la::NAME)));
                        if (!mod.lookupSymbol<mlir::func::FuncOp>(method_name)) {
                            auto decl = mlir::func::FuncOp::create(
                                loc_, method_name, make_fn_type(method));
                            mod.push_back(decl);
                        }
                    }
                }
            }
        }

        // Pass 2: fill function bodies.
        for (auto& [h, items] : all_modules) {
            holder_ = h;
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto item = map_of(items.get(i));
                int32_t item_code = code_of(item);

                if (item_code == la::FN) {
                    auto name = std::string(str_of(item.get(la::NAME)));
                    auto func = mod.lookupSymbol<mlir::func::FuncOp>(name);
                    if (!gen_function_body(func, item, {})) return nullptr;
                } else if (item_code == la::STRUCT) {
                    auto struct_name = std::string(str_of(item.get(la::NAME)));
                    if (!item.has_key(la::ITEMS)) continue;
                    auto methods_av = item.get(la::ITEMS);
                    if (methods_av.is_null() || !methods_av.is_pointer()) continue;
                    auto methods = arr_of(methods_av);
                    for (uint64_t m = 0; m < methods.size(); ++m) {
                        auto method = map_of(methods.get(m));
                        if (code_of(method) != la::FN) continue;
                        auto method_name = struct_name + "__" +
                                           std::string(str_of(method.get(la::NAME)));
                        auto func = mod.lookupSymbol<mlir::func::FuncOp>(method_name);
                        if (!gen_function_body(func, method, struct_name)) return nullptr;
                    }
                }
            }
        }

        if (mlir::failed(mlir::verify(mod))) {
            std::fprintf(stderr, "mlir_gen: module verification failed\n");
            mod.dump();
            return nullptr;
        }

        return mod;
    }

private:
    mlir::OpBuilder  builder_;
    mlir::Location   loc_;
    MemHolder*       holder_;

    // Struct type registry (module-level).
    std::unordered_map<std::string, StructInfo> struct_types_;
    // Module-level constants: name → (holder, VALUE node AnyVal).
    // The expression is re-evaluated (as constant) at each use site.
    struct ConstEntry { MemHolder* holder; AnyVal value_av; mlir::Type type; };
    std::unordered_map<std::string, ConstEntry> module_consts_;
    // Type aliases: name → resolved mlir::Type.
    std::unordered_map<std::string, mlir::Type> type_aliases_;

    // Per-function state — cleared for each function body.
    std::unordered_map<std::string, mlir::Value> scope_;         // name → ptr or SSA value
    std::unordered_set<std::string>              let_vars_;      // let-bound (alloca-backed)
    std::unordered_map<std::string, mlir::Type>  var_elem_types_;// scalar alloca elem type
    std::unordered_map<std::string, std::string> var_struct_;    // var → struct type name
    std::unordered_map<std::string, mlir::Type>  var_subscript_; // var → subscript elem type
    mlir::Type cur_ret_type_;                                    // current function return type (null = void)

    // Loop stack: {cond_block (or loop_block), exit_block}
    struct LoopBlocks { mlir::Block* cont; mlir::Block* exit; };
    std::vector<LoopBlocks> loop_stack_;

    int str_counter_ = 0;

    // ── MLIR helpers ─────────────────────────────────────────────

    // Block::getTerminator() in MLIR 21 returns the last op unconditionally.
    // Use hasTrait<IsTerminator> to check for actual terminators.
    static bool is_terminated(mlir::Block* block) noexcept {
        if (!block || block->empty()) return false;
        return block->back().hasTrait<mlir::OpTrait::IsTerminator>();
    }

    mlir::Value i32_zero() {
        return builder_.create<mlir::arith::ConstantIntOp>(loc_, int64_t(0), 32);
    }

    // Sign-extend or truncate integer value to target type.
    // Returns v unchanged if types are equal or either is not an integer type.
    mlir::Value coerce_int(mlir::Value v, mlir::Type to) {
        if (!v || !to || v.getType() == to) return v;
        auto from_int = mlir::dyn_cast<mlir::IntegerType>(v.getType());
        auto to_int   = mlir::dyn_cast<mlir::IntegerType>(to);
        if (!from_int || !to_int) return v;
        if (to_int.getWidth() > from_int.getWidth())
            return builder_.create<mlir::arith::ExtSIOp>(loc_, to, v);
        if (to_int.getWidth() < from_int.getWidth())
            return builder_.create<mlir::arith::TruncIOp>(loc_, to, v);
        return v;
    }

    mlir::Value i64_one() {
        return builder_.create<mlir::arith::ConstantIntOp>(loc_, int64_t(1), 64);
    }

    mlir::LLVM::LLVMPointerType ptr_type() {
        return mlir::LLVM::LLVMPointerType::get(builder_.getContext());
    }

    // ── Hermes helpers ───────────────────────────────────────────

    int32_t code_of(TinyMapView node) noexcept {
        AnyVal av = node.get(la::CODE);
        if (av.is_null()) return -1;
        return av.as_value<int32_t>();
    }

    std::string_view str_of(AnyVal av) noexcept {
        return StringView(av.to_offset(), holder_).view();
    }

    TinyMapView map_of(AnyVal av) noexcept {
        return TinyMapView(av.to_offset(), holder_);
    }

    ArrayView arr_of(AnyVal av) noexcept {
        return ArrayView(av.to_offset(), holder_);
    }

    // ── Type resolution ──────────────────────────────────────────

    mlir::Type resolve_type(TinyMapView type_ref) {
        int32_t tc = code_of(type_ref);

        if (tc == la::PTR_TYPE)
            return ptr_type();

        if (tc == la::ARR_TYPE) {
            // [T; N] — returns LLVM array type
            auto elem = resolve_type(map_of(type_ref.get(la::TYPE)));
            if (!elem) return nullptr;
            auto size_sv = str_of(type_ref.get(la::SIZE));
            uint64_t n = std::strtoull(size_sv.data(), nullptr, 10);
            return mlir::LLVM::LLVMArrayType::get(elem, n);
        }

        if (tc == la::TYPE_REF) {
            if (!type_ref.has_key(la::NAME) || type_ref.get(la::NAME).is_null())
                return nullptr;
            auto name = str_of(type_ref.get(la::NAME));
            if (name == "i32")  return builder_.getI32Type();
            if (name == "i64")  return builder_.getI64Type();
            if (name == "f64")  return builder_.getF64Type();
            if (name == "bool") return builder_.getI1Type();
            if (name == "u8")   return builder_.getIntegerType(8);
            if (name == "i8")   return builder_.getIntegerType(8);
            if (name == "u32")  return builder_.getIntegerType(32);
            if (name == "u64")  return builder_.getIntegerType(64);
            // Type alias
            auto ait = type_aliases_.find(std::string(name));
            if (ait != type_aliases_.end()) return ait->second;
            // Struct type → pass by pointer
            if (struct_types_.count(std::string(name)))
                return ptr_type();
            std::fprintf(stderr, "mlir_gen: unknown type '%.*s'\n",
                         (int)name.size(), name.data());
            return nullptr;
        }

        std::fprintf(stderr, "mlir_gen: unexpected type code %d\n", tc);
        return nullptr;
    }

    bool is_struct_type(TinyMapView type_ref, std::string& out_name) {
        if (code_of(type_ref) != la::TYPE_REF) return false;
        if (!type_ref.has_key(la::NAME) || type_ref.get(la::NAME).is_null()) return false;
        auto name = std::string(str_of(type_ref.get(la::NAME)));
        if (struct_types_.count(name)) { out_name = name; return true; }
        return false;
    }

    // ── Struct registration (Pass 0) ─────────────────────────────

    bool register_struct(TinyMapView node) {
        auto name = std::string(str_of(node.get(la::NAME)));
        if (struct_types_.count(name)) return true;

        auto struct_type = mlir::LLVM::LLVMStructType::getIdentified(
                               builder_.getContext(), name);
        StructInfo info;
        info.name = name;
        info.llvm_type = struct_type;

        if (node.has_key(la::FIELDS)) {
            AnyVal fields_av = node.get(la::FIELDS);
            if (!fields_av.is_null() && fields_av.is_pointer()) {
                auto fields_arr = arr_of(fields_av);
                llvm::SmallVector<mlir::Type> field_types;
                for (uint64_t i = 0; i < fields_arr.size(); ++i) {
                    auto fnode = map_of(fields_arr.get(i));
                    if (code_of(fnode) != la::FIELD_DEF) continue;
                    auto fname = std::string(str_of(fnode.get(la::NAME)));
                    auto ftype_node = map_of(fnode.get(la::TYPE));
                    auto ftype = resolve_type(ftype_node);
                    if (!ftype) {
                        std::fprintf(stderr, "mlir_gen: unknown field type in '%s'\n",
                                     name.c_str());
                        return false;
                    }
                    std::string fsname;
                    is_struct_type(ftype_node, fsname);
                    info.fields.push_back({fname, ftype, uint32_t(info.fields.size()), fsname});
                    field_types.push_back(ftype);
                }
                if (mlir::failed(struct_type.setBody(field_types, false))) {
                    std::fprintf(stderr, "mlir_gen: failed to set struct body for '%s'\n",
                                 name.c_str());
                    return false;
                }
            }
        }

        struct_types_[name] = std::move(info);
        return true;
    }

    // ── Parameter helpers ────────────────────────────────────────

    // Extract parameter AST nodes (raw) to inspect their types.
    void parse_params_raw(TinyMapView node,
                          std::vector<TinyMapView>& out_param_nodes) {
        if (!node.has_key(la::PARAMS)) return;
        AnyVal params_av = node.get(la::PARAMS);
        if (params_av.is_null() || !params_av.is_pointer()) return;
        auto wrapper = map_of(params_av);
        if (!wrapper.has_key(la::ITEMS)) return;
        AnyVal items_av = wrapper.get(la::ITEMS);
        if (items_av.is_null() || !items_av.is_pointer()) return;
        auto params = arr_of(items_av);
        for (uint64_t i = 0; i < params.size(); ++i)
            out_param_nodes.push_back(map_of(params.get(i)));
    }

    void parse_params(TinyMapView node,
                      llvm::SmallVectorImpl<mlir::Type>& types,
                      llvm::SmallVectorImpl<std::string>& names) {
        std::vector<TinyMapView> param_nodes;
        parse_params_raw(node, param_nodes);
        for (auto& p : param_nodes) {
            auto ptype = resolve_type(map_of(p.get(la::TYPE)));
            if (ptype) {
                types.push_back(ptype);
                names.push_back(std::string(str_of(p.get(la::NAME))));
            }
        }
    }

    mlir::FunctionType make_fn_type(TinyMapView node) {
        llvm::SmallVector<mlir::Type> param_types;
        llvm::SmallVector<std::string> param_names;
        parse_params(node, param_types, param_names);

        llvm::SmallVector<mlir::Type> ret_types;
        if (node.has_key(la::RET_TYPE) && !node.get(la::RET_TYPE).is_null()) {
            auto rt = resolve_type(map_of(node.get(la::RET_TYPE)));
            if (rt) ret_types.push_back(rt);
        }
        return builder_.getFunctionType(param_types, ret_types);
    }

    // ── Extern function ──────────────────────────────────────────

    mlir::func::FuncOp gen_extern_fn(TinyMapView node) {
        auto name = std::string(str_of(node.get(la::NAME)));
        auto fn = mlir::func::FuncOp::create(loc_, name, make_fn_type(node));
        fn.setPrivate();
        return fn;
    }

    // ── Function / method body ───────────────────────────────────

    bool gen_function_body(mlir::func::FuncOp func, TinyMapView node,
                           const std::string& struct_ctx) {
        llvm::SmallVector<mlir::Type> param_types;
        llvm::SmallVector<std::string> param_names;
        parse_params(node, param_types, param_names);

        auto* entry = func.addEntryBlock();
        builder_.setInsertionPointToStart(entry);

        scope_.clear();
        let_vars_.clear();
        var_elem_types_.clear();
        var_struct_.clear();
        var_subscript_.clear();

        // Bind parameters as SSA values (not alloca-backed).
        std::vector<TinyMapView> param_nodes;
        parse_params_raw(node, param_nodes);

        for (size_t i = 0; i < param_names.size(); ++i) {
            scope_[param_names[i]] = entry->getArgument(i);

            // Track subscript element type for pointer parameters.
            if (i < param_nodes.size()) {
                auto type_node = map_of(param_nodes[i].get(la::TYPE));
                if (code_of(type_node) == la::PTR_TYPE) {
                    // *mut T or *const T — infer element type for subscript
                    auto elem = resolve_type(map_of(type_node.get(la::POINTEE)));
                    if (elem) var_subscript_[param_names[i]] = elem;
                }
            }

            // Track struct type for 'self' parameter.
            if (!struct_ctx.empty() && param_names[i] == "self")
                var_struct_[param_names[i]] = struct_ctx;
        }

        auto body = map_of(node.get(la::BODY));
        auto ret_types = func.getFunctionType().getResults();
        cur_ret_type_ = ret_types.empty() ? mlir::Type{} : ret_types[0];
        loop_stack_.clear();
        gen_block(body, cur_ret_type_);

        if (!is_terminated(builder_.getBlock()))
            builder_.create<mlir::func::ReturnOp>(loc_);

        return true;
    }

    // ── Block ────────────────────────────────────────────────────

    // Returns the last produced value (may be null for void blocks).
    mlir::Value gen_block(TinyMapView block, mlir::Type /*hint*/) {
        auto items = arr_of(block.get(la::ITEMS));
        mlir::Value last = nullptr;
        for (uint64_t i = 0; i < items.size(); ++i)
            last = gen_stmt(map_of(items.get(i)));
        return last;
    }

    // ── Statements ───────────────────────────────────────────────

    mlir::Value gen_stmt(TinyMapView node) {
        int32_t code = code_of(node);
        switch (code) {
            case la::LET:         return gen_let(node);
            case la::RETURN:      return gen_return(node);
            case la::IF:          return gen_if_stmt(node);
            case la::WHILE:       return gen_while(node);
            case la::FOR:         return gen_for(node);
            case la::LOOP:        return gen_loop(node);
            case la::BREAK:       return gen_break();
            case la::CONTINUE:    return gen_continue();
            case la::ASSIGN:      return gen_assign(node);
            case la::FIELD_WRITE: return gen_field_write(node);
            case la::INDEX_WRITE: return gen_index_write(node);
            case la::EXPR_STMT:   return gen_expr(map_of(node.get(la::VALUE)));
            default:              return gen_expr(node);
        }
    }

    mlir::Value gen_let(TinyMapView node) {
        auto name = std::string(str_of(node.get(la::NAME)));
        auto val_node = map_of(node.get(la::VALUE));
        int32_t val_code = code_of(val_node);

        // ── Struct literal ─────────────────────────────────────────────
        if (val_code == la::STRUCT_LIT) {
            auto struct_name = std::string(str_of(val_node.get(la::NAME)));
            if (!struct_types_.count(struct_name)) {
                std::fprintf(stderr, "mlir_gen: unknown struct '%s'\n", struct_name.c_str());
                return nullptr;
            }
            auto alloca = gen_struct_lit(val_node);
            if (!alloca) return nullptr;
            scope_[name] = alloca;
            let_vars_.insert(name);
            var_struct_[name] = struct_name;
            return alloca;
        }

        // ── Array literal ──────────────────────────────────────────────
        if (val_code == la::ARR_LIT) {
            // Get element type from type annotation [T; N].
            mlir::Type elem_type = builder_.getI32Type();  // default
            if (node.has_key(la::TYPE) && !node.get(la::TYPE).is_null()) {
                auto tn = map_of(node.get(la::TYPE));
                if (code_of(tn) == la::ARR_TYPE)
                    elem_type = resolve_type(map_of(tn.get(la::TYPE)));
            }
            auto alloca = gen_arr_lit(val_node, elem_type);
            if (!alloca) return nullptr;
            scope_[name] = alloca;
            let_vars_.insert(name);
            var_subscript_[name] = elem_type;
            return alloca;
        }

        // ── Scalar value ───────────────────────────────────────────────
        auto val = gen_expr(val_node);
        if (!val) return nullptr;

        // If explicit type annotation, coerce (e.g. i32 literal → i64).
        if (node.has_key(la::TYPE) && !node.get(la::TYPE).is_null()) {
            auto tn = map_of(node.get(la::TYPE));
            if (code_of(tn) != la::ARR_TYPE) {  // arrays handled above
                auto ann = resolve_type(tn);
                if (ann) val = coerce_int(val, ann);
            }
        }

        auto elem_type = val.getType();
        auto alloca    = builder_.create<mlir::LLVM::AllocaOp>(
                             loc_, ptr_type(), elem_type, i64_one());
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);

        scope_[name] = alloca;
        let_vars_.insert(name);
        var_elem_types_[name] = elem_type;

        // Detect struct via type annotation.
        if (node.has_key(la::TYPE) && !node.get(la::TYPE).is_null()) {
            std::string sname;
            if (is_struct_type(map_of(node.get(la::TYPE)), sname))
                var_struct_[name] = sname;
        }
        return val;
    }

    mlir::Value gen_return(TinyMapView node) {
        if (node.has_key(la::VALUE) && !node.get(la::VALUE).is_null()) {
            auto val = gen_expr(map_of(node.get(la::VALUE)));
            if (val) {
                if (cur_ret_type_) val = coerce_int(val, cur_ret_type_);
                builder_.create<mlir::func::ReturnOp>(loc_, val);
            } else {
                builder_.create<mlir::func::ReturnOp>(loc_);
            }
        } else {
            builder_.create<mlir::func::ReturnOp>(loc_);
        }
        return nullptr;
    }

    mlir::Value gen_assign(TinyMapView node) {
        auto name = std::string(str_of(node.get(la::NAME)));
        if (!let_vars_.count(name)) {
            std::fprintf(stderr, "mlir_gen: assignment to non-let var '%s'\n", name.c_str());
            return nullptr;
        }
        auto val = gen_expr(map_of(node.get(la::VALUE)));
        if (!val) return nullptr;
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, scope_[name]);
        return val;
    }

    mlir::Value gen_field_write(TinyMapView node) {
        auto recv  = std::string(str_of(node.get(la::RECEIVER)));
        auto field = std::string(str_of(node.get(la::FIELD)));
        auto ptr   = get_struct_ptr(recv);
        if (!ptr) return nullptr;
        auto& info = struct_types_[var_struct_[recv]];
        auto gep   = gep_field(ptr, info, field);
        if (!gep) return nullptr;
        auto val   = gen_expr(map_of(node.get(la::VALUE)));
        if (!val) return nullptr;
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
        return val;
    }

    mlir::Value gen_index_write(TinyMapView node) {
        auto name  = std::string(str_of(node.get(la::NAME)));
        auto idx   = gen_expr(map_of(node.get(la::LHS)));
        auto val   = gen_expr(map_of(node.get(la::VALUE)));
        if (!idx || !val) return nullptr;
        auto arr_ptr = get_subscript_ptr(name);
        if (!arr_ptr) return nullptr;
        auto elem_type = subscript_elem_type(name);
        auto gep = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), elem_type, arr_ptr,
            llvm::ArrayRef<mlir::LLVM::GEPArg>{idx});
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
        return val;
    }

    mlir::Value gen_while(TinyMapView node) {
        auto* region      = builder_.getBlock()->getParent();
        auto* cond_block  = new mlir::Block();
        auto* body_block  = new mlir::Block();
        auto* exit_block  = new mlir::Block();
        region->push_back(cond_block);
        region->push_back(body_block);
        region->push_back(exit_block);

        builder_.create<mlir::cf::BranchOp>(loc_, cond_block);

        builder_.setInsertionPointToStart(cond_block);
        auto cond = gen_expr(map_of(node.get(la::COND)));
        if (!cond) return nullptr;
        builder_.create<mlir::cf::CondBranchOp>(loc_, cond, body_block, exit_block);

        builder_.setInsertionPointToStart(body_block);
        loop_stack_.push_back({cond_block, exit_block});
        gen_block(map_of(node.get(la::BODY)), nullptr);
        loop_stack_.pop_back();
        if (!is_terminated(builder_.getBlock()))
            builder_.create<mlir::cf::BranchOp>(loc_, cond_block);

        builder_.setInsertionPointToStart(exit_block);
        return nullptr;
    }

    // for i in lo..hi / lo..=hi
    // Lowers to: i = lo; while i < hi (or <=): body; i += 1
    mlir::Value gen_for(TinyMapView node) {
        auto var_name = std::string(str_of(node.get(la::NAME)));
        auto lo       = gen_expr(map_of(node.get(la::LHS)));
        auto hi       = gen_expr(map_of(node.get(la::RHS)));
        if (!lo || !hi) return nullptr;

        bool inclusive = false;
        if (node.has_key(la::INCLUSIVE)) {
            AnyVal av = node.get(la::INCLUSIVE);
            if (!av.is_null() && av.is_value())
                inclusive = av.as_value<uint8_t>() != 0;
        }

        // Allocate loop variable.
        auto i_alloca = builder_.create<mlir::LLVM::AllocaOp>(
                            loc_, ptr_type(), builder_.getI32Type(), i64_one());
        builder_.create<mlir::LLVM::StoreOp>(loc_, lo, i_alloca);
        scope_[var_name] = i_alloca;
        let_vars_.insert(var_name);
        var_elem_types_[var_name] = builder_.getI32Type();

        auto* region     = builder_.getBlock()->getParent();
        auto* cond_block = new mlir::Block();
        auto* body_block = new mlir::Block();
        auto* exit_block = new mlir::Block();
        region->push_back(cond_block);
        region->push_back(body_block);
        region->push_back(exit_block);

        builder_.create<mlir::cf::BranchOp>(loc_, cond_block);

        // Condition: i < hi (exclusive) or i <= hi (inclusive).
        builder_.setInsertionPointToStart(cond_block);
        auto i_val = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI32Type(), i_alloca);
        mlir::Value cond;
        if (inclusive)
            cond = builder_.create<mlir::arith::CmpIOp>(
                       loc_, mlir::arith::CmpIPredicate::sle, i_val, hi);
        else
            cond = builder_.create<mlir::arith::CmpIOp>(
                       loc_, mlir::arith::CmpIPredicate::slt, i_val, hi);
        builder_.create<mlir::cf::CondBranchOp>(loc_, cond, body_block, exit_block);

        // Body.
        builder_.setInsertionPointToStart(body_block);
        loop_stack_.push_back({cond_block, exit_block});
        gen_block(map_of(node.get(la::BODY)), nullptr);
        loop_stack_.pop_back();
        // Increment: i += 1.
        if (!is_terminated(builder_.getBlock())) {
            auto i_cur  = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI32Type(), i_alloca);
            auto one    = builder_.create<mlir::arith::ConstantOp>(
                              loc_, builder_.getI32Type(),
                              builder_.getI32IntegerAttr(1));
            auto i_next = builder_.create<mlir::arith::AddIOp>(loc_, i_cur, one);
            builder_.create<mlir::LLVM::StoreOp>(loc_, i_next, i_alloca);
            builder_.create<mlir::cf::BranchOp>(loc_, cond_block);
        }

        builder_.setInsertionPointToStart(exit_block);
        // Remove loop var from scope after loop.
        scope_.erase(var_name);
        let_vars_.erase(var_name);
        var_elem_types_.erase(var_name);
        return nullptr;
    }

    mlir::Value gen_loop(TinyMapView node) {
        auto* region      = builder_.getBlock()->getParent();
        auto* loop_block  = new mlir::Block();
        auto* exit_block  = new mlir::Block();
        region->push_back(loop_block);
        region->push_back(exit_block);

        builder_.create<mlir::cf::BranchOp>(loc_, loop_block);

        builder_.setInsertionPointToStart(loop_block);
        loop_stack_.push_back({loop_block, exit_block});
        gen_block(map_of(node.get(la::BODY)), nullptr);
        loop_stack_.pop_back();
        if (!is_terminated(builder_.getBlock()))
            builder_.create<mlir::cf::BranchOp>(loc_, loop_block);

        builder_.setInsertionPointToStart(exit_block);
        return nullptr;
    }

    mlir::Value gen_break() {
        if (loop_stack_.empty()) {
            std::fprintf(stderr, "mlir_gen: break outside loop\n");
            return nullptr;
        }
        builder_.create<mlir::cf::BranchOp>(loc_, loop_stack_.back().exit);
        return nullptr;
    }

    mlir::Value gen_continue() {
        if (loop_stack_.empty()) {
            std::fprintf(stderr, "mlir_gen: continue outside loop\n");
            return nullptr;
        }
        builder_.create<mlir::cf::BranchOp>(loc_, loop_stack_.back().cont);
        return nullptr;
    }

    // if-as-statement: no block argument on merge block.
    mlir::Value gen_if_stmt(TinyMapView node) {
        auto cond = gen_expr(map_of(node.get(la::COND)));
        if (!cond) return nullptr;

        auto* region      = builder_.getBlock()->getParent();
        auto* then_block  = new mlir::Block();
        auto* else_block  = new mlir::Block();
        auto* merge_block = new mlir::Block();
        region->push_back(then_block);
        region->push_back(else_block);
        region->push_back(merge_block);

        builder_.create<mlir::cf::CondBranchOp>(loc_, cond, then_block, else_block);

        // Then.
        builder_.setInsertionPointToStart(then_block);
        gen_block(map_of(node.get(la::THEN)), nullptr);
        bool then_falls_through = !is_terminated(builder_.getBlock());
        if (then_falls_through)
            builder_.create<mlir::cf::BranchOp>(loc_, merge_block);

        // Else.
        builder_.setInsertionPointToStart(else_block);
        bool has_else = node.has_key(la::ELSE) && !node.get(la::ELSE).is_null();
        if (has_else)
            gen_block(map_of(node.get(la::ELSE)), nullptr);
        bool else_falls_through = !is_terminated(builder_.getBlock());
        if (else_falls_through)
            builder_.create<mlir::cf::BranchOp>(loc_, merge_block);

        // If both branches terminated, merge_block is unreachable — drop it.
        // The caller will see the current block is terminated and won't add
        // a spurious trailing return.
        if (!then_falls_through && !else_falls_through) {
            merge_block->erase();
            return nullptr;
        }

        builder_.setInsertionPointToStart(merge_block);
        return nullptr;
    }

    // if-as-expression (e.g. let x = if c { 1 } else { 2 }): block argument.
    mlir::Value gen_if_expr(TinyMapView node) {
        auto cond = gen_expr(map_of(node.get(la::COND)));
        if (!cond) return nullptr;

        auto* region      = builder_.getBlock()->getParent();
        auto* then_block  = new mlir::Block();
        auto* else_block  = new mlir::Block();
        auto* merge_block = new mlir::Block();
        region->push_back(then_block);
        region->push_back(else_block);
        region->push_back(merge_block);

        mlir::Type res_type = builder_.getI32Type();
        merge_block->addArgument(res_type, loc_);

        builder_.create<mlir::cf::CondBranchOp>(loc_, cond, then_block, else_block);

        auto branch_with_val = [&](mlir::Value v) {
            if (!v) v = i32_zero();
            builder_.create<mlir::cf::BranchOp>(loc_, merge_block, mlir::ValueRange{v});
        };

        builder_.setInsertionPointToStart(then_block);
        auto then_val = gen_block(map_of(node.get(la::THEN)), res_type);
        if (!is_terminated(builder_.getBlock())) branch_with_val(then_val);

        builder_.setInsertionPointToStart(else_block);
        bool has_else = node.has_key(la::ELSE) && !node.get(la::ELSE).is_null();
        if (has_else) {
            auto else_val = gen_block(map_of(node.get(la::ELSE)), res_type);
            if (!is_terminated(builder_.getBlock())) branch_with_val(else_val);
        } else {
            branch_with_val(nullptr);  // no else → zero
        }

        builder_.setInsertionPointToStart(merge_block);
        return merge_block->getArgument(0);
    }

    // ── Expressions ──────────────────────────────────────────────

    mlir::Value gen_expr(TinyMapView node) {
        int32_t code = code_of(node);
        switch (code) {
            case la::LIT_INT:    return gen_lit_int(node);
            case la::LIT_BOOL:   return gen_lit_bool(node);
            case la::LIT_STR:    return gen_lit_str(node);
            case la::VAR_REF:    return gen_var_ref(node);
            case la::CALL:       return gen_call(node);
            case la::BINOP:      return gen_binop(node);
            case la::BLOCK:      return gen_block(node, nullptr);
            case la::IF:         return gen_if_expr(node);
            case la::FIELD_READ: return gen_field_read(node);
            case la::METHOD_CALL:return gen_method_call(node);
            case la::STRUCT_LIT: return gen_struct_lit(node);
            case la::INDEX_READ: return gen_index_read(node);
            case la::ARR_LIT:    return gen_arr_lit(node, builder_.getI32Type());
            case la::DEREF:      return gen_deref(node);
            case la::UNARY:      return gen_unary(node);
            case la::PAREN_EXPR: return gen_expr(map_of(node.get(la::VALUE)));
            case la::CAST:       return gen_cast(node);
            default:
                std::fprintf(stderr, "mlir_gen: unknown expr code %d\n", code);
                return nullptr;
        }
    }

    mlir::Value gen_lit_int(TinyMapView node) {
        auto text = str_of(node.get(la::VALUE));
        int64_t val = std::strtoll(text.data(), nullptr, 10);
        return builder_.create<mlir::arith::ConstantIntOp>(loc_, val, 32);
    }

    mlir::Value gen_lit_bool(TinyMapView node) {
        bool val = node.get(la::VALUE).as_value<uint8_t>() != 0;
        return builder_.create<mlir::arith::ConstantIntOp>(loc_, val ? 1 : 0, 1);
    }

    mlir::Value gen_lit_str(TinyMapView node) {
        auto raw = str_of(node.get(la::VALUE));
        std::string text(raw);
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
            text = text.substr(1, text.size() - 2);
        text.push_back('\0');

        auto global_name = ".str." + std::to_string(str_counter_++);
        auto parent_mod  = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
        auto save_pt     = builder_.saveInsertionPoint();
        builder_.setInsertionPointToStart(parent_mod.getBody());

        auto i8       = builder_.getIntegerType(8);
        auto arr_type = mlir::LLVM::LLVMArrayType::get(i8, text.size());
        auto str_attr = builder_.getStringAttr(llvm::StringRef(text.data(), text.size()));
        builder_.create<mlir::LLVM::GlobalOp>(
            loc_, arr_type, true, mlir::LLVM::Linkage::Internal, global_name, str_attr);

        builder_.restoreInsertionPoint(save_pt);
        return builder_.create<mlir::LLVM::AddressOfOp>(loc_, ptr_type(), global_name);
    }

    mlir::Value gen_var_ref(TinyMapView node) {
        auto name = std::string(str_of(node.get(la::NAME)));

        // Module-level constant: evaluate expression inline.
        auto cit = module_consts_.find(name);
        if (cit != module_consts_.end()) {
            auto& ce = cit->second;
            auto saved_holder = holder_;
            holder_ = ce.holder;
            auto val = gen_expr(map_of(ce.value_av));
            holder_ = saved_holder;
            return val;
        }

        auto it = scope_.find(name);
        if (it == scope_.end()) {
            std::fprintf(stderr, "mlir_gen: undefined variable '%s'\n", name.c_str());
            return nullptr;
        }

        // Struct / array variables: return the pointer directly.
        if (var_struct_.count(name) || var_subscript_.count(name))
            return it->second;

        // Let-bound scalar: load from alloca.
        if (let_vars_.count(name)) {
            auto et = var_elem_types_.find(name);
            if (et == var_elem_types_.end()) {
                std::fprintf(stderr, "mlir_gen: no elem type for '%s'\n", name.c_str());
                return nullptr;
            }
            return builder_.create<mlir::LLVM::LoadOp>(loc_, et->second, it->second);
        }

        // Parameter: return SSA value directly.
        return it->second;
    }

    mlir::Value gen_call(TinyMapView node) {
        auto callee = std::string(str_of(node.get(la::CALLEE)));
        llvm::SmallVector<mlir::Value> args;
        if (node.has_key(la::ARGS)) {
            auto arg_arr = arr_of(node.get(la::ARGS));
            for (uint64_t i = 0; i < arg_arr.size(); ++i) {
                auto v = gen_expr(map_of(arg_arr.get(i)));
                if (!v) return nullptr;
                args.push_back(v);
            }
        }
        auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
        auto callee_fn  = parent_mod.lookupSymbol<mlir::func::FuncOp>(callee);
        if (!callee_fn) {
            std::fprintf(stderr, "mlir_gen: undefined function '%s'\n", callee.c_str());
            return nullptr;
        }
        // Coerce integer arguments to expected param types.
        auto param_types = callee_fn.getFunctionType().getInputs();
        for (size_t i = 0; i < args.size() && i < param_types.size(); ++i)
            args[i] = coerce_int(args[i], param_types[i]);
        auto call = builder_.create<mlir::func::CallOp>(loc_, callee_fn, args);
        return call.getNumResults() > 0 ? call.getResult(0) : nullptr;
    }

    mlir::Value gen_binop(TinyMapView node) {
        auto lhs = gen_expr(map_of(node.get(la::LHS)));
        auto rhs = gen_expr(map_of(node.get(la::RHS)));
        if (!lhs || !rhs) return nullptr;
        // Widen narrower integer operand so MLIR types match.
        if (auto li = mlir::dyn_cast<mlir::IntegerType>(lhs.getType())) {
            if (auto ri = mlir::dyn_cast<mlir::IntegerType>(rhs.getType())) {
                if (li.getWidth() < ri.getWidth())
                    lhs = builder_.create<mlir::arith::ExtSIOp>(loc_, rhs.getType(), lhs);
                else if (ri.getWidth() < li.getWidth())
                    rhs = builder_.create<mlir::arith::ExtSIOp>(loc_, lhs.getType(), rhs);
            }
        }
        auto op = str_of(node.get(la::OP));
        if (op == "+")  return builder_.create<mlir::arith::AddIOp>(loc_, lhs, rhs);
        if (op == "-")  return builder_.create<mlir::arith::SubIOp>(loc_, lhs, rhs);
        if (op == "*")  return builder_.create<mlir::arith::MulIOp>(loc_, lhs, rhs);
        if (op == "/")  return builder_.create<mlir::arith::DivSIOp>(loc_, lhs, rhs);
        if (op == "%")  return builder_.create<mlir::arith::RemSIOp>(loc_, lhs, rhs);
        if (op == "&&") return builder_.create<mlir::arith::AndIOp>(loc_, lhs, rhs);
        if (op == "||") return builder_.create<mlir::arith::OrIOp> (loc_, lhs, rhs);
        if (op == "==") return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::eq,  lhs, rhs);
        if (op == "!=") return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::ne,  lhs, rhs);
        if (op == "<")  return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::slt, lhs, rhs);
        if (op == ">")  return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::sgt, lhs, rhs);
        if (op == "<=") return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::sle, lhs, rhs);
        if (op == ">=") return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::sge, lhs, rhs);
        std::fprintf(stderr, "mlir_gen: unknown op '%.*s'\n", (int)op.size(), op.data());
        return nullptr;
    }

    // ── Struct helpers ────────────────────────────────────────────

    mlir::Value get_struct_ptr(const std::string& name) {
        auto it = scope_.find(name);
        if (it == scope_.end()) {
            std::fprintf(stderr, "mlir_gen: undefined '%s'\n", name.c_str());
            return nullptr;
        }
        if (!var_struct_.count(name)) {
            std::fprintf(stderr, "mlir_gen: '%s' is not a struct var\n", name.c_str());
            return nullptr;
        }
        return it->second;
    }

    mlir::Value gep_field(mlir::Value base, const StructInfo& info,
                          const std::string& field_name) {
        for (auto& f : info.fields) {
            if (f.name == field_name) {
                llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(f.index)};
                return builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), info.llvm_type, base, idx);
            }
        }
        std::fprintf(stderr, "mlir_gen: struct '%s' has no field '%s'\n",
                     info.name.c_str(), field_name.c_str());
        return nullptr;
    }

    // Resolve a RECEIVER expression node to (struct_ptr, struct_name).
    // Handles VAR_REF (simple variable) and chained FIELD_READ (a.b.c).
    std::pair<mlir::Value, std::string> gen_recv_struct(TinyMapView recv_node) {
        int32_t code = code_of(recv_node);

        if (code == la::VAR_REF) {
            auto name = std::string(str_of(recv_node.get(la::NAME)));
            if (!var_struct_.count(name)) {
                std::fprintf(stderr, "mlir_gen: '%s' is not a struct var\n", name.c_str());
                return {nullptr, {}};
            }
            return {get_struct_ptr(name), var_struct_[name]};
        }

        if (code == la::FIELD_READ) {
            auto field = std::string(str_of(recv_node.get(la::FIELD)));
            auto [base_ptr, base_sname] = gen_recv_struct(map_of(recv_node.get(la::RECEIVER)));
            if (!base_ptr || base_sname.empty()) return {nullptr, {}};
            auto it = struct_types_.find(base_sname);
            if (it == struct_types_.end()) return {nullptr, {}};
            auto& info = it->second;
            auto gep = gep_field(base_ptr, info, field);
            if (!gep) return {nullptr, {}};
            // Determine the struct type of this field.
            for (auto& f : info.fields) {
                if (f.name == field) {
                    if (!f.struct_name.empty()) {
                        // Struct fields are stored as pointers; load the pointer
                        // to get the actual struct alloca before further GEP.
                        auto struct_ptr = builder_.create<mlir::LLVM::LoadOp>(
                                              loc_, ptr_type(), gep);
                        return {struct_ptr, f.struct_name};
                    }
                    std::fprintf(stderr, "mlir_gen: field '%s' is not a struct type\n",
                                 field.c_str());
                    return {nullptr, {}};
                }
            }
            return {nullptr, {}};
        }

        std::fprintf(stderr, "mlir_gen: unsupported receiver kind %d for struct access\n", code);
        return {nullptr, {}};
    }

    mlir::Value gen_field_read(TinyMapView node) {
        auto field = std::string(str_of(node.get(la::FIELD)));
        auto [ptr, sname] = gen_recv_struct(map_of(node.get(la::RECEIVER)));
        if (!ptr || sname.empty()) return nullptr;
        auto& info = struct_types_[sname];
        auto gep   = gep_field(ptr, info, field);
        if (!gep) return nullptr;
        for (auto& f : info.fields)
            if (f.name == field)
                return builder_.create<mlir::LLVM::LoadOp>(loc_, f.type, gep);
        return nullptr;
    }

    mlir::Value gen_method_call(TinyMapView node) {
        auto method = std::string(str_of(node.get(la::NAME)));
        auto [ptr, sname] = gen_recv_struct(map_of(node.get(la::RECEIVER)));
        if (!ptr || sname.empty()) return nullptr;
        auto mangled    = sname + "__" + method;
        auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
        auto callee_fn  = parent_mod.lookupSymbol<mlir::func::FuncOp>(mangled);
        if (!callee_fn) {
            std::fprintf(stderr, "mlir_gen: method '%s' not found\n", mangled.c_str());
            return nullptr;
        }
        llvm::SmallVector<mlir::Value> args;
        args.push_back(ptr);
        if (node.has_key(la::ARGS)) {
            auto arg_arr = arr_of(node.get(la::ARGS));
            for (uint64_t i = 0; i < arg_arr.size(); ++i) {
                auto v = gen_expr(map_of(arg_arr.get(i)));
                if (!v) return nullptr;
                args.push_back(v);
            }
        }
        auto call = builder_.create<mlir::func::CallOp>(loc_, callee_fn, args);
        return call.getNumResults() > 0 ? call.getResult(0) : nullptr;
    }

    mlir::Value gen_struct_lit(TinyMapView node) {
        auto sname = std::string(str_of(node.get(la::NAME)));
        auto sit   = struct_types_.find(sname);
        if (sit == struct_types_.end()) {
            std::fprintf(stderr, "mlir_gen: unknown struct '%s'\n", sname.c_str());
            return nullptr;
        }
        auto& info  = sit->second;
        auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                          loc_, ptr_type(), info.llvm_type, i64_one());
        if (node.has_key(la::ITEMS)) {
            AnyVal items_av = node.get(la::ITEMS);
            if (!items_av.is_null() && items_av.is_pointer()) {
                auto inits = arr_of(items_av);
                for (uint64_t i = 0; i < inits.size(); ++i) {
                    auto init = map_of(inits.get(i));
                    if (code_of(init) != la::FIELD_INIT) continue;
                    auto fname = std::string(str_of(init.get(la::NAME)));
                    auto val   = gen_expr(map_of(init.get(la::VALUE)));
                    if (!val) return nullptr;
                    auto gep = gep_field(alloca, info, fname);
                    if (!gep) return nullptr;
                    builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
                }
            }
        }
        return alloca;
    }

    // ── Array helpers ─────────────────────────────────────────────

    // Get pointer to arr[0] for subscript operations.
    // For let-bound arrays: scope_ holds the alloca (ptr to [N x T]).
    // For pointer params: scope_ holds the param pointer (ptr to T).
    mlir::Value get_subscript_ptr(const std::string& name) {
        auto it = scope_.find(name);
        if (it == scope_.end()) {
            std::fprintf(stderr, "mlir_gen: undefined '%s'\n", name.c_str());
            return nullptr;
        }
        return it->second;
    }

    mlir::Type subscript_elem_type(const std::string& name) {
        auto it = var_subscript_.find(name);
        if (it != var_subscript_.end()) return it->second;
        return builder_.getI32Type();  // default
    }

    // Generate array literal: alloca [N x T] and store each element.
    // Returns the alloca pointer.
    mlir::Value gen_arr_lit(TinyMapView node, mlir::Type elem_type) {
        if (!node.has_key(la::ITEMS)) return nullptr;
        AnyVal items_av = node.get(la::ITEMS);
        if (items_av.is_null() || !items_av.is_pointer()) return nullptr;
        auto items = arr_of(items_av);
        uint64_t n = items.size();

        auto arr_type = mlir::LLVM::LLVMArrayType::get(elem_type, n);
        auto alloca   = builder_.create<mlir::LLVM::AllocaOp>(
                            loc_, ptr_type(), arr_type, i64_one());

        for (uint64_t i = 0; i < n; ++i) {
            auto val = gen_expr(map_of(items.get(i)));
            if (!val) return nullptr;
            // GEP with constant index for initialization.
            llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(i)};
            auto gep = builder_.create<mlir::LLVM::GEPOp>(
                loc_, ptr_type(), elem_type, alloca, idx);
            builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
        }
        return alloca;
    }

    mlir::Value gen_index_read(TinyMapView node) {
        // RECEIVER is now an expression node (postfix-chain refactor).
        // For array vars it's a VAR_REF; get the variable name from it.
        TinyMapView recv_node = map_of(node.get(la::RECEIVER));
        mlir::Value arr_ptr;
        mlir::Type  elem_type;
        if (code_of(recv_node) == la::VAR_REF) {
            auto name  = std::string(str_of(recv_node.get(la::NAME)));
            arr_ptr    = get_subscript_ptr(name);
            elem_type  = subscript_elem_type(name);
        } else {
            // General expression receiver — evaluate and use as pointer.
            arr_ptr   = gen_expr(recv_node);
            elem_type = builder_.getI32Type(); // default; sema catches type errors
        }
        auto idx = gen_expr(map_of(node.get(la::VALUE)));
        if (!idx || !arr_ptr) return nullptr;
        llvm::SmallVector<mlir::LLVM::GEPArg> indices{idx};
        auto gep = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), elem_type, arr_ptr, indices);
        return builder_.create<mlir::LLVM::LoadOp>(loc_, elem_type, gep);
    }

    mlir::Value gen_deref(TinyMapView node) {
        auto ptr_val = gen_expr(map_of(node.get(la::VALUE)));
        if (!ptr_val) return nullptr;
        return builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI32Type(), ptr_val);
    }

    mlir::Value gen_cast(TinyMapView node) {
        auto val = gen_expr(map_of(node.get(la::VALUE)));
        if (!val) return nullptr;
        auto target = resolve_type(map_of(node.get(la::TYPE)));
        if (!target) {
            std::fprintf(stderr, "mlir_gen: cast: unknown target type\n");
            return nullptr;
        }
        if (val.getType() == target) return val;
        // Integer ↔ integer: sign-extend or truncate.
        if (auto from_int = mlir::dyn_cast<mlir::IntegerType>(val.getType())) {
            if (auto to_int = mlir::dyn_cast<mlir::IntegerType>(target)) {
                if (to_int.getWidth() > from_int.getWidth())
                    return builder_.create<mlir::arith::ExtSIOp>(loc_, target, val);
                if (to_int.getWidth() < from_int.getWidth())
                    return builder_.create<mlir::arith::TruncIOp>(loc_, target, val);
                return val;
            }
        }
        // Integer → float.
        if (mlir::dyn_cast<mlir::IntegerType>(val.getType()) && mlir::dyn_cast<mlir::FloatType>(target))
            return builder_.create<mlir::arith::SIToFPOp>(loc_, target, val);
        // Float → integer.
        if (mlir::dyn_cast<mlir::FloatType>(val.getType()) && mlir::dyn_cast<mlir::IntegerType>(target))
            return builder_.create<mlir::arith::FPToSIOp>(loc_, target, val);
        std::fprintf(stderr, "mlir_gen: unsupported cast\n");
        return nullptr;
    }

    mlir::Value gen_unary(TinyMapView node) {
        auto op = str_of(node.get(la::OP));

        // & (address-of) — does not evaluate operand, returns alloca pointer
        if (op == "&") {
            auto child = map_of(node.get(la::VALUE));
            if (code_of(child) != la::VAR_REF) {
                std::fprintf(stderr, "mlir_gen: & operand must be a variable\n");
                return nullptr;
            }
            auto name = std::string(str_of(child.get(la::NAME)));
            auto it = scope_.find(name);
            if (it == scope_.end()) {
                std::fprintf(stderr, "mlir_gen: & undefined '%s'\n", name.c_str());
                return nullptr;
            }
            return it->second;   // alloca pointer
        }

        auto val = gen_expr(map_of(node.get(la::VALUE)));
        if (!val) return nullptr;

        // - (arithmetic negation, i32)
        if (op == "-") {
            auto zero = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
            return builder_.create<mlir::arith::SubIOp>(loc_, zero, val);
        }

        // ! (logical NOT, i1)
        if (op == "!") {
            auto one = builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 1);
            return builder_.create<mlir::arith::XOrIOp>(loc_, val, one);
        }

        std::fprintf(stderr, "mlir_gen: unknown unary op '%.*s'\n",
                     (int)op.size(), op.data());
        return nullptr;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
mlir::OwningOpRef<mlir::ModuleOp> mlir_gen(mlir::MLIRContext& ctx,
                                            const std::vector<hermes::HermesCtr>& asts) noexcept
{
    MLIRGenImpl gen(ctx);
    return gen.generate(asts);
}

} // namespace logos::compiler
