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

    mlir::OwningOpRef<mlir::ModuleOp> generate(hermes::HermesCtrView ast) {
        holder_ = ast.holder();

        auto mod = mlir::ModuleOp::create(loc_);

        auto root_obj = ast.root_object();
        auto root = root_obj.as_tiny_map();
        int32_t code = code_of(root);

        if (code != la::MODULE) {
            std::fprintf(stderr, "mlir_gen: expected MODULE node, got code %d\n", code);
            return nullptr;
        }

        auto items = arr_of(root.get(la::ITEMS));
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto item = map_of(items.get(i));
            int32_t item_code = code_of(item);
            if (item_code == la::FN) {
                auto fn = gen_function(item);
                if (!fn) return nullptr;
                mod.push_back(fn);
            } else {
                std::fprintf(stderr, "mlir_gen: unknown top-level item code %d\n", item_code);
                return nullptr;
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
        auto name = str_of(type_ref.get(la::NAME));
        if (name == "i32")  return builder_.getI32Type();
        if (name == "i64")  return builder_.getI64Type();
        if (name == "f64")  return builder_.getF64Type();
        if (name == "bool") return builder_.getI1Type();
        std::fprintf(stderr, "mlir_gen: unknown type '%.*s'\n",
                     (int)name.size(), name.data());
        return nullptr;
    }

    // -- Function -----------------------------------------------------
    mlir::func::FuncOp gen_function(TinyMapView node) {
        auto name = std::string(str_of(node.get(la::NAME)));

        // Parameters.
        llvm::SmallVector<mlir::Type> param_types;
        llvm::SmallVector<std::string> param_names;
        if (node.has_key(la::PARAMS)) {
            auto params = arr_of(node.get(la::PARAMS));
            for (uint64_t i = 0; i < params.size(); ++i) {
                auto p = map_of(params.get(i));
                auto ptype = resolve_type(map_of(p.get(la::TYPE)));
                if (!ptype) return nullptr;
                param_types.push_back(ptype);
                param_names.push_back(std::string(str_of(p.get(la::NAME))));
            }
        }

        // Return type.
        llvm::SmallVector<mlir::Type> ret_types;
        if (node.has_key(la::RET_TYPE)) {
            auto rt = resolve_type(map_of(node.get(la::RET_TYPE)));
            if (!rt) return nullptr;
            ret_types.push_back(rt);
        }

        auto func_type = builder_.getFunctionType(param_types, ret_types);
        auto func = mlir::func::FuncOp::create(loc_, name, func_type);

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
        auto result = gen_block(body, ret_types.empty() ? nullptr : ret_types[0]);

        // If block didn't terminate, add implicit return.
        if (!builder_.getBlock()->getTerminator()) {
            if (result) {
                builder_.create<mlir::func::ReturnOp>(loc_, result);
            } else {
                builder_.create<mlir::func::ReturnOp>(loc_);
            }
        }

        return func;
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
            case la::LET:    return gen_let(node);
            case la::RETURN: return gen_return(node);
            case la::IF:     return gen_if(node);
            default:         return gen_expr(node);
        }
    }

    mlir::Value gen_let(TinyMapView node) {
        auto name = std::string(str_of(node.get(la::NAME)));
        auto val  = gen_expr(map_of(node.get(la::VALUE)));
        if (val) scope_[name] = val;
        return val;
    }

    mlir::Value gen_return(TinyMapView node) {
        if (node.has_key(la::VALUE)) {
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
                                            hermes::HermesCtrView ast) noexcept
{
    MLIRGenImpl gen(ctx);
    return gen.generate(ast);
}

} // namespace logos::compiler
