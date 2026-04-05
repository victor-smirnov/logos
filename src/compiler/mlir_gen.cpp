// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// MLIRGen — lower Logos Hermes AST to MLIR.
//
// Iteration 1: free functions, i32/i64/f64/bool, let, if/else, return,
// binary ops, function calls.  No custom dialect — emits directly into
// func/arith/scf/cf standard dialects.

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
#include <string>
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

        // Collect (holder, items) pairs from all ASTs.
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

        // Pass 1: emit all declarations (extern fn + fn forward decl).
        for (auto& [h, items] : all_modules) {
            holder_ = h;
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto item = map_of(items.get(i));
                int32_t item_code = code_of(item);
                if (item_code == la::EXTERN_FN) {
                    // Skip if already declared (duplicate extern across modules).
                    auto name = std::string(str_of(item.get(la::NAME)));
                    if (!mod.lookupSymbol<mlir::func::FuncOp>(name)) {
                        auto fn = gen_extern_fn(item);
                        if (!fn) return nullptr;
                        mod.push_back(fn);
                    }
                } else if (item_code == la::FN) {
                    auto fn_type = make_fn_type(item);
                    auto name = std::string(str_of(item.get(la::NAME)));
                    if (!mod.lookupSymbol<mlir::func::FuncOp>(name)) {
                        auto decl = mlir::func::FuncOp::create(loc_, name, fn_type);
                        mod.push_back(decl);
                    }
                }
            }
        }

        // Pass 2: fill in function bodies.
        for (auto& [h, items] : all_modules) {
            holder_ = h;
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto item = map_of(items.get(i));
                int32_t item_code = code_of(item);
                if (item_code == la::FN) {
                    auto name = std::string(str_of(item.get(la::NAME)));
                    auto func = mod.lookupSymbol<mlir::func::FuncOp>(name);
                    if (!gen_function_body(func, item)) return nullptr;
                } else if (item_code == la::USE || item_code == la::PACKAGE) {
                    // Skip — handled by module loader.
                } else if (item_code != la::EXTERN_FN) {
                    std::fprintf(stderr, "mlir_gen: unknown top-level item code %d\n", item_code);
                    return nullptr;
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

    // Variable name -> SSA value in current scope.
    std::unordered_map<std::string, mlir::Value> scope_;
    int str_counter_ = 0;  // unique suffix for string global names

    // -- Hermes access helpers ----------------------------------------
    //
    // TinyMapView.get(NamedCode<uint8_t>) is the checked overload.
    // TinyMapView.has_key(NamedCode<uint8_t>) checks existence.

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

    // -- Type mapping -------------------------------------------------
    mlir::Type resolve_type(TinyMapView type_ref) {
        int32_t tc = code_of(type_ref);

        // *const T / *mut T → LLVM pointer type (opaque ptr in LLVM 21)
        if (tc == la::PTR_TYPE) {
            return mlir::LLVM::LLVMPointerType::get(builder_.getContext());
        }

        // Simple type reference by name.
        if (!type_ref.has_key(la::NAME) || type_ref.get(la::NAME).is_null()) {
            return nullptr;
        }
        auto name = str_of(type_ref.get(la::NAME));
        if (name == "i32")  return builder_.getI32Type();
        if (name == "i64")  return builder_.getI64Type();
        if (name == "f64")  return builder_.getF64Type();
        if (name == "bool") return builder_.getI1Type();
        if (name == "u8")   return builder_.getIntegerType(8);
        std::fprintf(stderr, "mlir_gen: unknown type '%.*s'\n",
                     (int)name.size(), name.data());
        return nullptr;
    }

    // Helper: parse parameters from AST node (shared by fn and extern fn).
    void parse_params(TinyMapView node,
                      llvm::SmallVectorImpl<mlir::Type>& types,
                      llvm::SmallVectorImpl<std::string>& names) {
        if (!node.has_key(la::PARAMS)) return;
        AnyVal params_av = node.get(la::PARAMS);
        if (params_av.is_null() || !params_av.is_pointer()) return;

        // param_list returns { ITEMS: [...params...] } wrapper node.
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

    // -- Extern function (FFI declaration, no body) -------------------
    mlir::func::FuncOp gen_extern_fn(TinyMapView node) {
        auto name = std::string(str_of(node.get(la::NAME)));
        auto func_type = make_fn_type(node);
        auto func = mlir::func::FuncOp::create(loc_, name, func_type);
        func.setPrivate();  // extern = not defined here
        return func;
    }

    // -- Function body ------------------------------------------------
    bool gen_function_body(mlir::func::FuncOp func, TinyMapView node) {
        llvm::SmallVector<mlir::Type> param_types;
        llvm::SmallVector<std::string> param_names;
        parse_params(node, param_types, param_names);

        // Entry block with arguments.
        auto* entry = func.addEntryBlock();
        builder_.setInsertionPointToStart(entry);

        // Bind parameter names.
        scope_.clear();
        for (size_t i = 0; i < param_names.size(); ++i) {
            scope_[param_names[i]] = entry->getArgument(i);
        }

        // Generate body.
        auto body = map_of(node.get(la::BODY));
        auto ret_types = func.getFunctionType().getResults();
        auto result = gen_block(body, ret_types.empty() ? nullptr : ret_types[0]);

        // If block didn't terminate, add implicit return.
        if (!builder_.getBlock()->getTerminator()) {
            if (result) {
                builder_.create<mlir::func::ReturnOp>(loc_, result);
            } else {
                builder_.create<mlir::func::ReturnOp>(loc_);
            }
        }

        return true;
    }

    // -- Block --------------------------------------------------------
    mlir::Value gen_block(TinyMapView block, mlir::Type expected_type) {
        auto items = arr_of(block.get(la::ITEMS));
        mlir::Value last = nullptr;
        for (uint64_t i = 0; i < items.size(); ++i) {
            last = gen_stmt(map_of(items.get(i)));
        }
        return last;
    }

    // -- Statements ---------------------------------------------------
    mlir::Value gen_stmt(TinyMapView node) {
        int32_t code = code_of(node);
        switch (code) {
            case la::LET:       return gen_let(node);
            case la::RETURN:    return gen_return(node);
            case la::IF:        return gen_if(node);
            case la::EXPR_STMT: return gen_expr(map_of(node.get(la::VALUE)));
            default:            return gen_expr(node);
        }
    }

    mlir::Value gen_let(TinyMapView node) {
        auto name = std::string(str_of(node.get(la::NAME)));
        auto val  = gen_expr(map_of(node.get(la::VALUE)));
        if (val) scope_[name] = val;
        return val;
    }

    mlir::Value gen_return(TinyMapView node) {
        if (node.has_key(la::VALUE) && !node.get(la::VALUE).is_null()) {
            auto val = gen_expr(map_of(node.get(la::VALUE)));
            builder_.create<mlir::func::ReturnOp>(loc_, val);
        } else {
            builder_.create<mlir::func::ReturnOp>(loc_);
        }
        return nullptr;
    }

    mlir::Value gen_if(TinyMapView node) {
        auto cond = gen_expr(map_of(node.get(la::COND)));

        // Simple if/else with cf.cond_br -> two blocks -> merge.
        auto* region = builder_.getBlock()->getParent();
        auto* then_block = new mlir::Block();
        auto* else_block = new mlir::Block();
        auto* merge_block = new mlir::Block();
        region->push_back(then_block);
        region->push_back(else_block);
        region->push_back(merge_block);

        auto then_body = map_of(node.get(la::THEN));
        bool has_else = node.has_key(la::ELSE);

        // Determine result type from then-block.
        // For iteration 1, if-as-expression returns i32.
        mlir::Type res_type = builder_.getI32Type();
        merge_block->addArgument(res_type, loc_);

        builder_.create<mlir::cf::CondBranchOp>(loc_, cond, then_block, else_block);

        // Then.
        builder_.setInsertionPointToStart(then_block);
        auto then_val = gen_block(then_body, res_type);
        if (!builder_.getBlock()->getTerminator()) {
            builder_.create<mlir::cf::BranchOp>(loc_, merge_block,
                                                 then_val ? mlir::ValueRange{then_val}
                                                          : mlir::ValueRange{});
        }

        // Else.
        builder_.setInsertionPointToStart(else_block);
        if (has_else) {
            auto else_body = map_of(node.get(la::ELSE));
            auto else_val = gen_block(else_body, res_type);
            if (!builder_.getBlock()->getTerminator()) {
                builder_.create<mlir::cf::BranchOp>(loc_, merge_block,
                                                     else_val ? mlir::ValueRange{else_val}
                                                              : mlir::ValueRange{});
            }
        } else {
            // No else — branch to merge with zero.
            auto zero = builder_.create<mlir::arith::ConstantIntOp>(loc_, int64_t(0), 32);
            builder_.create<mlir::cf::BranchOp>(loc_, merge_block, mlir::ValueRange{zero});
        }

        // Merge.
        builder_.setInsertionPointToStart(merge_block);
        return merge_block->getArgument(0);
    }

    // -- Expressions --------------------------------------------------
    mlir::Value gen_expr(TinyMapView node) {
        int32_t code = code_of(node);
        switch (code) {
            case la::LIT_INT:  return gen_lit_int(node);
            case la::LIT_BOOL: return gen_lit_bool(node);
            case la::LIT_STR:  return gen_lit_str(node);
            case la::VAR_REF:  return gen_var_ref(node);
            case la::CALL:     return gen_call(node);
            case la::BINOP:    return gen_binop(node);
            case la::BLOCK:    return gen_block(node, nullptr);
            case la::IF:       return gen_if(node);
            default:
                std::fprintf(stderr, "mlir_gen: unknown expr code %d\n", code);
                return nullptr;
        }
    }

    mlir::Value gen_lit_int(TinyMapView node) {
        // PEG parser stores INTEGER token capture as an arena string.
        // Parse the string to get the numeric value.
        auto text = str_of(node.get(la::VALUE));
        int64_t val = std::strtoll(text.data(), nullptr, 10);
        return builder_.create<mlir::arith::ConstantIntOp>(loc_, val, 32);
    }

    mlir::Value gen_lit_bool(TinyMapView node) {
        // PEG parser stores bool literals as AnyVal::from_value(uint8_t(0/1)).
        AnyVal av = node.get(la::VALUE);
        bool val = av.as_value<uint8_t>() != 0;
        return builder_.create<mlir::arith::ConstantIntOp>(loc_, val ? 1 : 0, 1);
    }

    mlir::Value gen_lit_str(TinyMapView node) {
        // String literal → LLVM global constant + addressof → *const u8.
        // PEG parser stores the string WITH quotes; strip them.
        auto raw = str_of(node.get(la::VALUE));
        std::string text(raw);
        // Strip surrounding quotes.
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
            text = text.substr(1, text.size() - 2);
        // Append null terminator for C interop.
        text.push_back('\0');

        // Create a unique global name.
        auto global_name = std::string(".str.") + std::to_string(str_counter_++);

        // Get the parent module to insert the global.
        auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
        auto save_point = builder_.saveInsertionPoint();

        // Insert global at module level.
        builder_.setInsertionPointToStart(parent_mod.getBody());

        auto i8 = builder_.getIntegerType(8);
        auto arr_type = mlir::LLVM::LLVMArrayType::get(i8, text.size());
        auto str_attr = builder_.getStringAttr(llvm::StringRef(text.data(), text.size()));

        auto global = builder_.create<mlir::LLVM::GlobalOp>(
            loc_, arr_type, /*isConstant=*/true,
            mlir::LLVM::Linkage::Internal, global_name, str_attr);
        (void)global;

        // Restore insertion point and emit addressof.
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

        // Look up function in the parent module.
        auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
        auto callee_fn = parent_mod.lookupSymbol<mlir::func::FuncOp>(callee);
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

        // PEG parser stores OP as an arena string (token capture).
        auto op = str_of(node.get(la::OP));

        // Integer arithmetic.
        if (op == "+")  return builder_.create<mlir::arith::AddIOp>(loc_, lhs, rhs);
        if (op == "-")  return builder_.create<mlir::arith::SubIOp>(loc_, lhs, rhs);
        if (op == "*")  return builder_.create<mlir::arith::MulIOp>(loc_, lhs, rhs);
        if (op == "/")  return builder_.create<mlir::arith::DivSIOp>(loc_, lhs, rhs);

        // Integer comparison -> i1.
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
