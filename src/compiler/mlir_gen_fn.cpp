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

mlir::Type MLIRGenImpl::fn_call_ret_llvm_type(TypeRef ret_type) {
    if (!ret_type) return nullptr;
    TypeRef rv{ret_type};
    if (type_str(ret_type) == "AnyVal") return builder_.getI32Type();
    if (rv.kind() == LogosType::Kind::Tuple) {
        return tuple_llvm_type(ret_type);
    }
    if (rv.kind() == LogosType::Kind::Struct ||
        rv.kind() == LogosType::Kind::ZonedStruct) {
        auto cname = mlir_struct_key(ret_type);
        auto sit = struct_types_.find(cname);
        if (sit != struct_types_.end()) return sit->second.llvm_type;
        return ptr_type();
    }
    if (rv.kind() == LogosType::Kind::Enum) {
        auto* te = resolve_tagged_enum(std::string(rv.enum_name()), ret_type);
        if (te) return te->llvm_type;
        return builder_.getI32Type();
    }
    return logos_to_mlir(ret_type);
}

mlir::FunctionType MLIRGenImpl::make_fn_type(const LFunction& fn) {
    llvm::SmallVector<mlir::Type> param_types;
    for (auto& p : fn.params) {
        if (p.type && type_str(p.type) == "AnyVal") {
            param_types.push_back(builder_.getI32Type());
            continue;
        }
        // Arrays (like structs) are passed by pointer.
        if (p.type && TypeRef(p.type).kind() == LogosType::Kind::Array)
            param_types.push_back(ptr_type());
        else {
            auto t = logos_to_mlir(p.type);
            if (t) param_types.push_back(t);
        }
    }
    llvm::SmallVector<mlir::Type> ret_types;
    if (fn.ret_type) {
        TypeRef rv{fn.ret_type};
        if (type_str(fn.ret_type) == "AnyVal") {
            ret_types.push_back(builder_.getI32Type());
        } else
        // Tuples and structs are returned by value (as LLVM struct), not by pointer.
        // Returning a pointer to a local alloca would be a dangling pointer after return.
        if (rv.kind() == LogosType::Kind::Tuple) {
            auto rt = tuple_llvm_type(fn.ret_type);
            if (rt) ret_types.push_back(rt);
        } else if (rv.kind() == LogosType::Kind::Struct ||
                   rv.kind() == LogosType::Kind::ZonedStruct) {
            auto cname = mlir_struct_key(fn.ret_type);
            auto sit = struct_types_.find(cname);
            if (sit != struct_types_.end())
                ret_types.push_back(sit->second.llvm_type);
            else
                ret_types.push_back(ptr_type()); // fallback (struct not yet registered)
        } else if (rv.kind() == LogosType::Kind::Enum) {
            // Tagged enums must also be returned by value (aggregate), not by pointer.
            auto* te = resolve_tagged_enum(std::string(rv.enum_name()), fn.ret_type);
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

void MLIRGenImpl::forward_declare(mlir::ModuleOp mod, const LFunction& fn,
                                    bool is_binary_skip) {
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
    // Binary-skip and extern fns are declaration-only — set private at
    // creation time to avoid the separate setPrivate-by-name pass.
    if (fn.is_extern || is_binary_skip) f.setPrivate();
    mod.push_back(f);
    // Record Logos-level param types for dyn coercion at call sites.
    std::vector<TypeRef> ptypes;
    for (auto& p : fn.params) ptypes.push_back(p.type);
    fn_param_types_[fn.name] = std::move(ptypes);
}

// ---------------------------------------------------------------------------
// Function body
// ---------------------------------------------------------------------------

bool MLIRGenImpl::gen_function_body(mlir::func::FuncOp func, const LFunction& fn) {
    // Guard: two distinct LFunctions producing the same mangled name would
    // otherwise both call addEntryBlock on the same FuncOp, resulting in a
    // single function with two unrelated bodies stitched together. Bug
    // surfaces later as a bare MLIR verifier "func.return op expects parent
    // op func.func" error with no source location. Most common cause:
    // pkg-mangling skips a non-current package context for a free fn, so
    // a private fn in pkg A collides with a pub fn of the same base name
    // in pkg B (which A imports).
    if (!func.empty()) {
        std::fprintf(stderr,
            "mlir-gen: duplicate function body for symbol '%s'; two "
            "functions resolved to the same mangled name — likely a "
            "private fn in one package shadowed by a pub fn of the same "
            "base name in an imported package. Rename one to disambiguate.\n",
            fn.name.c_str());
        return false;
    }
    auto* entry = func.addEntryBlock();
    builder_.setInsertionPointToStart(entry);
    cur_entry_block_ = entry;

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
    ref_param_names_.clear();
    loop_stack_.clear();

    // Bind parameters.
    for (size_t i = 0; i < fn.params.size(); ++i) {
        auto& p = fn.params[i];
        scope_[p.name] = entry->getArgument(i);
        // Record Ref/MutRef-typed params for the EAddrOfView spill path.
        if (p.type) {
            auto pk = TypeRef(p.type).kind();
            if (pk == LogosType::Kind::Ref || pk == LogosType::Kind::MutRef)
                ref_param_names_.insert(p.name);
        }

        // Track subscript element type for pointer / reference parameters.
        auto is_ptr_kind = [](LogosType::Kind k) {
            return k == LogosType::Kind::Ptr ||
                   k == LogosType::Kind::Ref ||
                   k == LogosType::Kind::MutRef;
        };
        if (p.type) {
            TypeRef pv{p.type};
            if (is_ptr_kind(pv.kind()) && pv.pointee()) {
                // For ptr-to-struct, the subscript stride must be
                // sizeof(struct), not sizeof(ptr) — `logos_to_mlir(Struct)`
                // collapses to ptr_type, so look up the struct's full LLVM
                // type directly. Params don't go through a local alloca
                // slot, so we register only var_subscript_ (gen_index_*
                // reads it directly off the SSA arg) — not var_local_ptrs_,
                // which would trigger a spurious LoadOp.
                TypeRef pe = pv.pointee();
                mlir::Type et;
                if (pe.kind() == LogosType::Kind::Struct ||
                    pe.kind() == LogosType::Kind::ZonedStruct) {
                    auto cname = mlir_struct_key(pe);
                    auto sit = struct_types_.find(cname);
                    if (sit != struct_types_.end()) et = sit->second.llvm_type;
                }
                if (!et) et = logos_to_mlir(pe);
                if (et) var_subscript_[p.name] = et;
            } else if (pv.kind() == LogosType::Kind::Array && pv.elem()) {
                // Array params arrive as `ptr` (per make_fn_type). Without an
                // explicit subscript entry, gen_index_read's
                // subscript_elem_type(name) falls back to i32 — which on an
                // i64 array reads with stride-4 instead of stride-8 and
                // yields the alternating-value/zero pattern that masked the
                // assertion bug. Register the element's MLIR type so the
                // GEP stride matches the array layout.
                auto et = logos_to_mlir(pv.elem());
                if (et) var_subscript_[p.name] = et;
            }
        }

        // Track struct / class type for parameters (including 'self').
        if (p.type) {
            TypeRef pv{p.type};
            std::string sname;
            if (pv.kind() == LogosType::Kind::Struct ||
                pv.kind() == LogosType::Kind::ZonedStruct)
                sname = mlir_struct_key(p.type);
            else if (is_ptr_kind(pv.kind()) && pv.pointee() &&
                     (pv.pointee().kind() == LogosType::Kind::Struct ||
                      pv.pointee().kind() == LogosType::Kind::ZonedStruct))
                sname = mlir_struct_key(pv.pointee());
            if (!sname.empty()) { var_struct_[p.name] = std::move(sname); continue; }

        }
    }

    auto ret_types = func.getFunctionType().getResults();
    cur_ret_type_ = ret_types.empty() ? mlir::Type{} : ret_types[0];
    cur_fn_ret_logos_type_ = fn.ret_type;
    cur_fn_name_ = fn.name;

    gen_block(block_ref_of(fn.body));

    if (!is_terminated(builder_.getBlock())) {
        if (ret_types.empty()) {
            builder_.create<mlir::func::ReturnOp>(loc_);
        } else {
            // Non-void fn whose body fell through. Sema's reachability
            // pass should have rejected genuinely missing returns; the
            // fall-through here means sema accepted the path (e.g.
            // exhaustive tuple/struct match without an explicit `_`
            // arm) but mlir-gen's dispatch lowering doesn't know that
            // and left the merge block live. Emit unreachable so the
            // function verifies; dead code, never executed.
            builder_.create<mlir::LLVM::UnreachableOp>(loc_);
        }
    }

    return true;
}

} // namespace logos::compiler
