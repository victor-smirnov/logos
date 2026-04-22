// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// mlir_gen_fn.cpp — malloc/free helpers, function type, forward declaration, function body.

#include "mlir_gen_impl.hpp"

namespace logos::compiler {

using namespace lir;

// ---------------------------------------------------------------------------
// malloc / free helpers
// ---------------------------------------------------------------------------

void MLIRGenImpl::ensure_malloc_free(mlir::ModuleOp mod) {
    if (!mod.lookupSymbol("malloc")) {
        auto fn_type = builder_.getFunctionType(
            {builder_.getI64Type()}, {ptr_type()});
        auto fn = mlir::func::FuncOp::create(loc_, "malloc", fn_type);
        fn.setPrivate();
        mod.push_back(fn);
    }
    if (!mod.lookupSymbol("free")) {
        auto fn_type = builder_.getFunctionType({ptr_type()}, {});
        auto fn = mlir::func::FuncOp::create(loc_, "free", fn_type);
        fn.setPrivate();
        mod.push_back(fn);
    }
}

mlir::Value MLIRGenImpl::call_malloc(mlir::Value size) {
    auto mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    auto malloc_fn = mod.lookupSymbol<mlir::func::FuncOp>("malloc");
    if (!malloc_fn) return nullptr;
    auto call = builder_.create<mlir::func::CallOp>(
        loc_, malloc_fn, mlir::ValueRange{size});
    return call.getResult(0);
}

void MLIRGenImpl::call_free(mlir::Value ptr) {
    auto mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    auto free_fn = mod.lookupSymbol<mlir::func::FuncOp>("free");
    if (!free_fn) return;
    builder_.create<mlir::func::CallOp>(loc_, free_fn, mlir::ValueRange{ptr});
}

// Compute sizeof an LLVM struct type via GEP null trick.
mlir::Value MLIRGenImpl::sizeof_struct(mlir::LLVM::LLVMStructType struct_type) {
    mlir::Value zero64 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
    mlir::Value null   = builder_.create<mlir::LLVM::IntToPtrOp>(loc_, ptr_type(), zero64);
    llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(1)};
    mlir::Value gep = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), struct_type, null, idx);
    return builder_.create<mlir::LLVM::PtrToIntOp>(
        loc_, builder_.getI64Type(), gep);
}

// ---------------------------------------------------------------------------
// Function type from LFunction
// ---------------------------------------------------------------------------

mlir::FunctionType MLIRGenImpl::make_fn_type(const LFunction& fn) {
    llvm::SmallVector<mlir::Type> param_types;
    for (auto& p : fn.params) {
        if (p.type && type_str(p.type) == "AnyVal") {
            param_types.push_back(builder_.getI32Type());
            continue;
        }
        // Arrays (like structs) are passed by pointer.
        if (p.type && p.type->kind == LogosType::Kind::Array)
            param_types.push_back(ptr_type());
        else {
            auto t = logos_to_mlir(p.type);
            if (t) param_types.push_back(t);
        }
    }
    llvm::SmallVector<mlir::Type> ret_types;
    if (fn.ret_type) {
        if (type_str(fn.ret_type) == "AnyVal") {
            ret_types.push_back(builder_.getI32Type());
        } else
        // Tuples and structs are returned by value (as LLVM struct), not by pointer.
        // Returning a pointer to a local alloca would be a dangling pointer after return.
        if (fn.ret_type->kind == LogosType::Kind::Tuple) {
            auto rt = tuple_llvm_type(fn.ret_type);
            if (rt) ret_types.push_back(rt);
        } else if (fn.ret_type->kind == LogosType::Kind::Struct ||
                   fn.ret_type->kind == LogosType::Kind::ZonedStruct) {
            auto cname = concrete_struct_name(fn.ret_type);
            auto sit = struct_types_.find(cname);
            if (sit != struct_types_.end())
                ret_types.push_back(sit->second.llvm_type);
            else
                ret_types.push_back(ptr_type()); // fallback (struct not yet registered)
        } else if (fn.ret_type->kind == LogosType::Kind::Enum) {
            // Tagged enums must also be returned by value (aggregate), not by pointer.
            auto* te = resolve_tagged_enum(fn.ret_type->enum_name, fn.ret_type);
            if (te)
                ret_types.push_back(te->llvm_type);
            else {
                // C-style (non-payload) enum — return i32.
                ret_types.push_back(builder_.getI32Type());
            }
        } else {
            auto rt = logos_to_mlir(fn.ret_type);
            if (rt) ret_types.push_back(rt);
        }
    }
    return builder_.getFunctionType(param_types, ret_types);
}

// ---------------------------------------------------------------------------
// Forward declare
// ---------------------------------------------------------------------------

void MLIRGenImpl::forward_declare(mlir::ModuleOp mod, const LFunction& fn) {
    if (fn.is_vararg) {
        // Vararg extern fns use llvm.func (func dialect doesn't support varargs)
        if (mod.lookupSymbol<mlir::LLVM::LLVMFuncOp>(fn.name)) return;
        llvm::SmallVector<mlir::Type> param_types;
        for (auto& p : fn.params) {
            auto t = logos_to_mlir(p.type);
            if (t) param_types.push_back(t);
        }
        mlir::Type ret = fn.ret_type ? logos_to_mlir(fn.ret_type) : nullptr;
        if (!ret) ret = mlir::LLVM::LLVMVoidType::get(builder_.getContext());
        auto llvm_fn_type = mlir::LLVM::LLVMFunctionType::get(ret, param_types,
                                                                /*isVarArg=*/true);
        auto llvm_fn = builder_.create<mlir::LLVM::LLVMFuncOp>(loc_, fn.name, llvm_fn_type);
        llvm_fn.setLinkage(mlir::LLVM::Linkage::External);
        mod.push_back(llvm_fn);
        vararg_fns_.insert(fn.name);
        return;
    }
    if (mod.lookupSymbol<mlir::func::FuncOp>(fn.name)) return;
    auto f = mlir::func::FuncOp::create(loc_, fn.name, make_fn_type(fn));
    if (fn.is_extern) f.setPrivate();
    mod.push_back(f);
    // Record Logos-level param types for dyn coercion at call sites.
    std::vector<const LogosType*> ptypes;
    for (auto& p : fn.params) ptypes.push_back(p.type);
    fn_param_types_[fn.name] = std::move(ptypes);
}

// ---------------------------------------------------------------------------
// Function body
// ---------------------------------------------------------------------------

bool MLIRGenImpl::gen_function_body(mlir::func::FuncOp func, const LFunction& fn) {
    auto* entry = func.addEntryBlock();
    builder_.setInsertionPointToStart(entry);

    scope_.clear();
    let_vars_.clear();
    var_elem_types_.clear();
    var_struct_.clear();
    var_class_.clear();
    var_subscript_.clear();
    var_tuple_.clear();
    var_tagged_enum_.clear();
    var_tagged_enum_ptr_.clear();
    var_local_ptrs_.clear();
    var_dyn_trait_.clear();
    loop_stack_.clear();

    // Bind parameters.
    for (size_t i = 0; i < fn.params.size(); ++i) {
        auto& p = fn.params[i];
        scope_[p.name] = entry->getArgument(i);

        // Track subscript element type for pointer / reference parameters.
        auto is_ptr_kind = [](LogosType::Kind k) {
            return k == LogosType::Kind::Ptr ||
                   k == LogosType::Kind::Ref ||
                   k == LogosType::Kind::MutRef;
        };
        if (p.type && is_ptr_kind(p.type->kind) && p.type->pointee) {
            auto et = logos_to_mlir(p.type->pointee);
            if (et) var_subscript_[p.name] = et;
        }

        // Track struct / class type for parameters (including 'self').
        if (p.type) {
            std::string sname;
            if (p.type->kind == LogosType::Kind::Struct ||
                p.type->kind == LogosType::Kind::ZonedStruct)
                sname = concrete_struct_name(p.type);
            else if (is_ptr_kind(p.type->kind) && p.type->pointee &&
                     (p.type->pointee->kind == LogosType::Kind::Struct ||
                      p.type->pointee->kind == LogosType::Kind::ZonedStruct))
                sname = concrete_struct_name(p.type->pointee);
            if (!sname.empty()) { var_struct_[p.name] = std::move(sname); continue; }

        }
    }

    auto ret_types = func.getFunctionType().getResults();
    cur_ret_type_ = ret_types.empty() ? mlir::Type{} : ret_types[0];
    cur_fn_ret_logos_type_ = fn.ret_type;

    gen_block(fn.body);

    if (!is_terminated(builder_.getBlock()))
        builder_.create<mlir::func::ReturnOp>(loc_);

    return true;
}

} // namespace logos::compiler
