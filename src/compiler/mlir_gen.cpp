// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// MLIRGen — lower Logos Hermes AST to MLIR.
//
// Iteration 3: structs, methods, field access, while loops, assignment,
// struct literals.  All let bindings use alloca so that reassignment and
// loop-carried state work correctly (LLVM mem2reg handles the cleanup).

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

        // Collect all modules (holder + item array pairs).
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

        // Pass 0: register all struct types (needed before codegen for type resolution).
        for (auto& [h, items] : all_modules) {
            holder_ = h;
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto item = map_of(items.get(i));
                if (code_of(item) == la::STRUCT) {
                    if (!register_struct(item)) return nullptr;
                }
            }
        }

        // Pass 1: emit all declarations (extern fn + fn/method forward decls).
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
                    // Forward-declare all methods.
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
                            auto fn_type = make_method_fn_type(struct_name, method);
                            auto decl = mlir::func::FuncOp::create(loc_, method_name, fn_type);
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

    // Struct type registry.
    std::unordered_map<std::string, StructInfo> struct_types_;

    // Per-function state — cleared for each function body.
    std::unordered_map<std::string, mlir::Value> scope_;       // name → value (ptr or SSA)
    std::unordered_set<std::string>              let_vars_;    // let-bound (alloca-backed)
    std::unordered_map<std::string, mlir::Type>  var_elem_types_; // scalar elem type
    std::unordered_map<std::string, std::string> var_struct_;  // var → struct type name

    int str_counter_ = 0;

    // ── MLIR helpers ─────────────────────────────────────────────

    // In MLIR 21, Block::getTerminator() returns the last op unconditionally
    // (not checking IsTerminator trait). Use hasTrait directly instead.
    static bool is_terminated(mlir::Block* block) noexcept {
        if (!block || block->empty()) return false;
        return block->back().hasTrait<mlir::OpTrait::IsTerminator>();
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

        if (tc == la::PTR_TYPE) {
            return mlir::LLVM::LLVMPointerType::get(builder_.getContext());
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

            // Struct type — resolved as LLVM pointer (structs passed by pointer).
            auto sit = struct_types_.find(std::string(name));
            if (sit != struct_types_.end()) {
                return mlir::LLVM::LLVMPointerType::get(builder_.getContext());
            }

            std::fprintf(stderr, "mlir_gen: unknown type '%.*s'\n",
                         (int)name.size(), name.data());
            return nullptr;
        }

        std::fprintf(stderr, "mlir_gen: unexpected type code %d\n", tc);
        return nullptr;
    }

    // Returns true if type_ref names a known struct type.
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
        if (struct_types_.count(name)) return true;  // already registered

        // Create the identified struct type (body set later once all fields resolved).
        auto struct_type = mlir::LLVM::LLVMStructType::getIdentified(
                               builder_.getContext(), name);

        // Collect fields.
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
                        std::fprintf(stderr, "mlir_gen: unknown field type in struct '%s'\n",
                                     name.c_str());
                        return false;
                    }
                    info.fields.push_back({fname, ftype, uint32_t(info.fields.size())});
                    field_types.push_back(ftype);
                }
                if (mlir::failed(struct_type.setBody(field_types, /*packed=*/false))) {
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

    void parse_params(TinyMapView node,
                      llvm::SmallVectorImpl<mlir::Type>& types,
                      llvm::SmallVectorImpl<std::string>& names) {
        if (!node.has_key(la::PARAMS)) return;
        AnyVal params_av = node.get(la::PARAMS);
        if (params_av.is_null() || !params_av.is_pointer()) return;

        auto wrapper = map_of(params_av);
        if (!wrapper.has_key(la::ITEMS)) return;
        AnyVal items_av = wrapper.get(la::ITEMS);
        if (items_av.is_null() || !items_av.is_pointer()) return;

        auto params = arr_of(items_av);
        for (uint64_t i = 0; i < params.size(); ++i) {
            auto p = map_of(params.get(i));
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

    // Method: like fn_type but strips the 'self' pointer param (it's already a ptr type).
    mlir::FunctionType make_method_fn_type(const std::string& /*struct_name*/,
                                            TinyMapView node) {
        // Methods are just regular functions in MLIR — params include self.
        return make_fn_type(node);
    }

    // ── Extern function ──────────────────────────────────────────

    mlir::func::FuncOp gen_extern_fn(TinyMapView node) {
        auto name = std::string(str_of(node.get(la::NAME)));
        auto fn = mlir::func::FuncOp::create(loc_, name, make_fn_type(node));
        fn.setPrivate();
        return fn;
    }

    // ── Function / method body ───────────────────────────────────

    // struct_ctx: non-empty for methods (the owning struct name).
    bool gen_function_body(mlir::func::FuncOp func, TinyMapView node,
                           const std::string& struct_ctx) {
        llvm::SmallVector<mlir::Type> param_types;
        llvm::SmallVector<std::string> param_names;
        parse_params(node, param_types, param_names);

        auto* entry = func.addEntryBlock();
        builder_.setInsertionPointToStart(entry);

        // Reset per-function scope.
        scope_.clear();
        let_vars_.clear();
        var_elem_types_.clear();
        var_struct_.clear();

        // Bind parameters as SSA values (not alloca-backed).
        for (size_t i = 0; i < param_names.size(); ++i) {
            scope_[param_names[i]] = entry->getArgument(i);
            // If a param is a pointer to a known struct, track that.
            // (For self params, 'self: *const Point' — the struct_ctx tells us.)
            // We detect this at field_read/method_call time from var_struct_ if set.
            // For method 'self' param, record the struct type.
            if (!struct_ctx.empty() && param_names[i] == "self") {
                var_struct_[param_names[i]] = struct_ctx;
            }
        }

        // Generate body.
        auto body = map_of(node.get(la::BODY));
        auto ret_types = func.getFunctionType().getResults();
        auto result = gen_block(body, ret_types.empty() ? nullptr : ret_types[0]);

        // Implicit return if block didn't terminate.
        if (!is_terminated(builder_.getBlock())) {
            if (result) builder_.create<mlir::func::ReturnOp>(loc_, result);
            else        builder_.create<mlir::func::ReturnOp>(loc_);
        }

        return true;
    }

    // ── Block ────────────────────────────────────────────────────

    mlir::Value gen_block(TinyMapView block, mlir::Type /*expected_type*/) {
        auto items = arr_of(block.get(la::ITEMS));
        mlir::Value last = nullptr;
        for (uint64_t i = 0; i < items.size(); ++i) {
            last = gen_stmt(map_of(items.get(i)));
        }
        return last;
    }

    // ── Statements ───────────────────────────────────────────────

    mlir::Value gen_stmt(TinyMapView node) {
        int32_t code = code_of(node);
        switch (code) {
            case la::LET:        return gen_let(node);
            case la::RETURN:     return gen_return(node);
            case la::IF:         return gen_if(node);
            case la::WHILE:      return gen_while(node);
            case la::ASSIGN:     return gen_assign(node);
            case la::FIELD_WRITE:return gen_field_write(node);
            case la::EXPR_STMT:  return gen_expr(map_of(node.get(la::VALUE)));
            default:             return gen_expr(node);
        }
    }

    mlir::Value gen_let(TinyMapView node) {
        auto name = std::string(str_of(node.get(la::NAME)));
        auto val_node = map_of(node.get(la::VALUE));
        int32_t val_code = code_of(val_node);

        // ── Struct literal: alloca the struct, store fields, bind ptr ──
        if (val_code == la::STRUCT_LIT) {
            auto struct_name = std::string(str_of(val_node.get(la::NAME)));
            auto sit = struct_types_.find(struct_name);
            if (sit == struct_types_.end()) {
                std::fprintf(stderr, "mlir_gen: unknown struct '%s'\n", struct_name.c_str());
                return nullptr;
            }
            auto ptr = gen_struct_lit(val_node);
            if (!ptr) return nullptr;
            scope_[name] = ptr;
            let_vars_.insert(name);
            var_struct_[name] = struct_name;
            return ptr;
        }

        // ── Scalar: generate value, alloca, store ─────────────────────
        auto val = gen_expr(val_node);
        if (!val) return nullptr;

        auto elem_type = val.getType();
        auto ptr_type = mlir::LLVM::LLVMPointerType::get(builder_.getContext());
        auto one = builder_.create<mlir::arith::ConstantIntOp>(loc_, int64_t(1), 64);
        auto alloca = builder_.create<mlir::LLVM::AllocaOp>(loc_, ptr_type, elem_type, one);
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);

        scope_[name] = alloca;
        let_vars_.insert(name);
        var_elem_types_[name] = elem_type;

        // If type annotation names a struct, record it.
        if (node.has_key(la::TYPE) && !node.get(la::TYPE).is_null()) {
            std::string sname;
            if (is_struct_type(map_of(node.get(la::TYPE)), sname)) {
                var_struct_[name] = sname;
            }
        }
        return val;
    }

    mlir::Value gen_return(TinyMapView node) {
        if (node.has_key(la::VALUE) && !node.get(la::VALUE).is_null()) {
            auto val = gen_expr(map_of(node.get(la::VALUE)));
            if (val) builder_.create<mlir::func::ReturnOp>(loc_, val);
            else     builder_.create<mlir::func::ReturnOp>(loc_);
        } else {
            builder_.create<mlir::func::ReturnOp>(loc_);
        }
        return nullptr;
    }

    mlir::Value gen_assign(TinyMapView node) {
        auto name = std::string(str_of(node.get(la::NAME)));
        auto val = gen_expr(map_of(node.get(la::VALUE)));
        if (!val) return nullptr;

        if (!let_vars_.count(name)) {
            std::fprintf(stderr, "mlir_gen: assignment to non-let variable '%s'\n", name.c_str());
            return nullptr;
        }
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, scope_[name]);
        return val;
    }

    mlir::Value gen_field_write(TinyMapView node) {
        auto receiver_name = std::string(str_of(node.get(la::RECEIVER)));
        auto field_name    = std::string(str_of(node.get(la::FIELD)));

        auto ptr = get_struct_ptr(receiver_name);
        if (!ptr) return nullptr;

        auto& info = struct_types_[var_struct_[receiver_name]];
        auto gep   = gep_field(ptr, info, field_name);
        if (!gep) return nullptr;

        auto val = gen_expr(map_of(node.get(la::VALUE)));
        if (!val) return nullptr;

        builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
        return val;
    }

    mlir::Value gen_while(TinyMapView node) {
        auto* region    = builder_.getBlock()->getParent();
        auto* cond_block = new mlir::Block();
        auto* body_block = new mlir::Block();
        auto* exit_block = new mlir::Block();
        region->push_back(cond_block);
        region->push_back(body_block);
        region->push_back(exit_block);

        // Jump into condition block.
        builder_.create<mlir::cf::BranchOp>(loc_, cond_block);

        // Condition block.
        builder_.setInsertionPointToStart(cond_block);
        auto cond = gen_expr(map_of(node.get(la::COND)));
        if (!cond) return nullptr;
        builder_.create<mlir::cf::CondBranchOp>(loc_, cond, body_block, exit_block);

        // Body block.
        builder_.setInsertionPointToStart(body_block);
        auto body = map_of(node.get(la::BODY));
        gen_block(body, nullptr);
        if (!is_terminated(builder_.getBlock()))
            builder_.create<mlir::cf::BranchOp>(loc_, cond_block);

        // Exit block.
        builder_.setInsertionPointToStart(exit_block);
        return nullptr;
    }

    mlir::Value gen_if(TinyMapView node) {
        auto cond = gen_expr(map_of(node.get(la::COND)));

        auto* region     = builder_.getBlock()->getParent();
        auto* then_block  = new mlir::Block();
        auto* else_block  = new mlir::Block();
        auto* merge_block = new mlir::Block();
        region->push_back(then_block);
        region->push_back(else_block);
        region->push_back(merge_block);

        bool has_else = node.has_key(la::ELSE) && !node.get(la::ELSE).is_null();
        mlir::Type res_type = builder_.getI32Type();
        merge_block->addArgument(res_type, loc_);

        builder_.create<mlir::cf::CondBranchOp>(loc_, cond, then_block, else_block);

        // Then.
        builder_.setInsertionPointToStart(then_block);
        auto then_val = gen_block(map_of(node.get(la::THEN)), res_type);
        if (!is_terminated(builder_.getBlock()))
            builder_.create<mlir::cf::BranchOp>(loc_, merge_block,
                then_val ? mlir::ValueRange{then_val} : mlir::ValueRange{});

        // Else.
        builder_.setInsertionPointToStart(else_block);
        if (has_else) {
            auto else_val = gen_block(map_of(node.get(la::ELSE)), res_type);
            if (!is_terminated(builder_.getBlock()))
                builder_.create<mlir::cf::BranchOp>(loc_, merge_block,
                    else_val ? mlir::ValueRange{else_val} : mlir::ValueRange{});
        } else {
            auto zero = builder_.create<mlir::arith::ConstantIntOp>(loc_, int64_t(0), 32);
            builder_.create<mlir::cf::BranchOp>(loc_, merge_block, mlir::ValueRange{zero});
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
            case la::IF:         return gen_if(node);
            case la::FIELD_READ: return gen_field_read(node);
            case la::METHOD_CALL:return gen_method_call(node);
            case la::STRUCT_LIT: return gen_struct_lit(node);
            case la::DEREF:      return gen_deref(node);
            case la::PAREN_EXPR: return gen_expr(map_of(node.get(la::VALUE)));
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
        AnyVal av = node.get(la::VALUE);
        bool val = av.as_value<uint8_t>() != 0;
        return builder_.create<mlir::arith::ConstantIntOp>(loc_, val ? 1 : 0, 1);
    }

    mlir::Value gen_lit_str(TinyMapView node) {
        auto raw = str_of(node.get(la::VALUE));
        std::string text(raw);
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
            text = text.substr(1, text.size() - 2);
        text.push_back('\0');

        auto global_name = std::string(".str.") + std::to_string(str_counter_++);
        auto parent_mod  = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
        auto save_point  = builder_.saveInsertionPoint();
        builder_.setInsertionPointToStart(parent_mod.getBody());

        auto i8       = builder_.getIntegerType(8);
        auto arr_type = mlir::LLVM::LLVMArrayType::get(i8, text.size());
        auto str_attr = builder_.getStringAttr(llvm::StringRef(text.data(), text.size()));
        builder_.create<mlir::LLVM::GlobalOp>(
            loc_, arr_type, /*isConstant=*/true,
            mlir::LLVM::Linkage::Internal, global_name, str_attr);

        builder_.restoreInsertionPoint(save_point);
        auto ptr_type = mlir::LLVM::LLVMPointerType::get(builder_.getContext());
        return builder_.create<mlir::LLVM::AddressOfOp>(loc_, ptr_type, global_name);
    }

    mlir::Value gen_var_ref(TinyMapView node) {
        auto name = std::string(str_of(node.get(la::NAME)));
        auto it = scope_.find(name);
        if (it == scope_.end()) {
            std::fprintf(stderr, "mlir_gen: undefined variable '%s'\n", name.c_str());
            return nullptr;
        }

        // Struct variables: return the pointer directly (not loaded).
        if (var_struct_.count(name)) return it->second;

        // Let-bound scalar: load from alloca.
        if (let_vars_.count(name)) {
            auto elem_type_it = var_elem_types_.find(name);
            if (elem_type_it == var_elem_types_.end()) {
                // Fallback: shouldn't happen for scalars.
                std::fprintf(stderr, "mlir_gen: no elem type for let var '%s'\n", name.c_str());
                return nullptr;
            }
            return builder_.create<mlir::LLVM::LoadOp>(loc_, elem_type_it->second, it->second);
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

        auto call = builder_.create<mlir::func::CallOp>(loc_, callee_fn, args);
        return call.getNumResults() > 0 ? call.getResult(0) : nullptr;
    }

    mlir::Value gen_binop(TinyMapView node) {
        auto lhs = gen_expr(map_of(node.get(la::LHS)));
        auto rhs = gen_expr(map_of(node.get(la::RHS)));
        if (!lhs || !rhs) return nullptr;

        auto op = str_of(node.get(la::OP));
        if (op == "+")  return builder_.create<mlir::arith::AddIOp>(loc_, lhs, rhs);
        if (op == "-")  return builder_.create<mlir::arith::SubIOp>(loc_, lhs, rhs);
        if (op == "*")  return builder_.create<mlir::arith::MulIOp>(loc_, lhs, rhs);
        if (op == "/")  return builder_.create<mlir::arith::DivSIOp>(loc_, lhs, rhs);
        if (op == "==") return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::eq,  lhs, rhs);
        if (op == "!=") return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::ne,  lhs, rhs);
        if (op == "<")  return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::slt, lhs, rhs);
        if (op == ">")  return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::sgt, lhs, rhs);
        if (op == "<=") return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::sle, lhs, rhs);
        if (op == ">=") return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::sge, lhs, rhs);

        std::fprintf(stderr, "mlir_gen: unknown operator '%.*s'\n",
                     (int)op.size(), op.data());
        return nullptr;
    }

    // ── Struct helpers ────────────────────────────────────────────

    // Get the struct pointer for a variable (alloca ptr or parameter ptr).
    mlir::Value get_struct_ptr(const std::string& name) {
        auto it = scope_.find(name);
        if (it == scope_.end()) {
            std::fprintf(stderr, "mlir_gen: undefined variable '%s'\n", name.c_str());
            return nullptr;
        }
        if (!var_struct_.count(name)) {
            std::fprintf(stderr, "mlir_gen: '%s' is not a struct variable\n", name.c_str());
            return nullptr;
        }
        // For let-bound struct vars, scope_ holds the alloca ptr.
        // For struct pointer parameters (self), scope_ holds the ptr directly.
        // Either way, it's a pointer to the struct.
        return it->second;
    }

    // Emit GEP to get a pointer to a named field of a struct.
    mlir::Value gep_field(mlir::Value ptr, const StructInfo& info,
                          const std::string& field_name) {
        // Find field index.
        const FieldInfo* fi = nullptr;
        for (auto& f : info.fields) {
            if (f.name == field_name) { fi = &f; break; }
        }
        if (!fi) {
            std::fprintf(stderr, "mlir_gen: struct '%s' has no field '%s'\n",
                         info.name.c_str(), field_name.c_str());
            return nullptr;
        }

        auto ptr_type = mlir::LLVM::LLVMPointerType::get(builder_.getContext());
        // GEP indices: [0, field_index] — first 0 is the array element index.
        llvm::SmallVector<mlir::LLVM::GEPArg> indices{int32_t(0), int32_t(fi->index)};
        return builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type, info.llvm_type, ptr, indices);
    }

    mlir::Value gen_field_read(TinyMapView node) {
        auto receiver_name = std::string(str_of(node.get(la::RECEIVER)));
        auto field_name    = std::string(str_of(node.get(la::FIELD)));

        // Find the struct type for this receiver.
        std::string sname;
        if (var_struct_.count(receiver_name))
            sname = var_struct_[receiver_name];
        else {
            std::fprintf(stderr, "mlir_gen: receiver '%s' is not a struct variable\n",
                         receiver_name.c_str());
            return nullptr;
        }

        auto& info = struct_types_[sname];
        auto ptr = get_struct_ptr(receiver_name);
        if (!ptr) return nullptr;

        auto gep = gep_field(ptr, info, field_name);
        if (!gep) return nullptr;

        // Find field type.
        for (auto& f : info.fields) {
            if (f.name == field_name) {
                return builder_.create<mlir::LLVM::LoadOp>(loc_, f.type, gep);
            }
        }
        return nullptr;
    }

    mlir::Value gen_method_call(TinyMapView node) {
        auto receiver_name = std::string(str_of(node.get(la::RECEIVER)));
        auto method_name   = std::string(str_of(node.get(la::NAME)));

        // Determine struct type.
        std::string sname;
        if (var_struct_.count(receiver_name))
            sname = var_struct_[receiver_name];
        else {
            std::fprintf(stderr, "mlir_gen: receiver '%s' is not a struct variable\n",
                         receiver_name.c_str());
            return nullptr;
        }

        auto mangled = sname + "__" + method_name;
        auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
        auto callee_fn  = parent_mod.lookupSymbol<mlir::func::FuncOp>(mangled);
        if (!callee_fn) {
            std::fprintf(stderr, "mlir_gen: method '%s' not found\n", mangled.c_str());
            return nullptr;
        }

        // First arg: the struct pointer.
        llvm::SmallVector<mlir::Value> args;
        auto ptr = get_struct_ptr(receiver_name);
        if (!ptr) return nullptr;
        args.push_back(ptr);

        // Remaining args.
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

    // Generate a struct literal: alloca + GEP + store for each field.
    // Returns the alloca pointer.
    mlir::Value gen_struct_lit(TinyMapView node) {
        auto struct_name = std::string(str_of(node.get(la::NAME)));
        auto sit = struct_types_.find(struct_name);
        if (sit == struct_types_.end()) {
            std::fprintf(stderr, "mlir_gen: unknown struct '%s'\n", struct_name.c_str());
            return nullptr;
        }
        auto& info = sit->second;

        // Alloca one instance of the struct.
        auto ptr_type = mlir::LLVM::LLVMPointerType::get(builder_.getContext());
        auto one      = builder_.create<mlir::arith::ConstantIntOp>(loc_, int64_t(1), 64);
        auto alloca   = builder_.create<mlir::LLVM::AllocaOp>(
                            loc_, ptr_type, info.llvm_type, one);

        // Store each field initializer.
        if (node.has_key(la::ITEMS)) {
            AnyVal items_av = node.get(la::ITEMS);
            if (!items_av.is_null() && items_av.is_pointer()) {
                auto inits = arr_of(items_av);
                for (uint64_t i = 0; i < inits.size(); ++i) {
                    auto init_node = map_of(inits.get(i));
                    if (code_of(init_node) != la::FIELD_INIT) continue;

                    auto fname = std::string(str_of(init_node.get(la::NAME)));
                    auto val   = gen_expr(map_of(init_node.get(la::VALUE)));
                    if (!val) return nullptr;

                    auto gep = gep_field(alloca, info, fname);
                    if (!gep) return nullptr;
                    builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
                }
            }
        }

        return alloca;
    }

    mlir::Value gen_deref(TinyMapView node) {
        auto ptr = gen_expr(map_of(node.get(la::VALUE)));
        if (!ptr) return nullptr;
        // We need to know the pointee type. For now, default to i32.
        // Full type inference would require a type-checking pass.
        auto elem_type = builder_.getI32Type();
        return builder_.create<mlir::LLVM::LoadOp>(loc_, elem_type, ptr);
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
