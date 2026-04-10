// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// MLIRGen — lower Logos L-IR to MLIR.
//
// Input: lir::LProgram (typed IR produced by sema_lower()).
// Output: mlir::ModuleOp.
//
// All type information is pre-computed in L-IR — no Hermes lookups here.
//
// This file: generate(), struct/array helpers, public mlir_gen() function.
// Method definitions split across mlir_gen_types.cpp, mlir_gen_fn.cpp,
// mlir_gen_stmt.cpp, mlir_gen_expr.cpp, mlir_gen_dyn.cpp.

#include "mlir_gen_impl.hpp"

namespace logos::compiler {

using namespace lir;

// ---------------------------------------------------------------------------
// MLIRGenImpl::generate — top-level lowering pipeline
// ---------------------------------------------------------------------------

mlir::OwningOpRef<mlir::ModuleOp> MLIRGenImpl::generate(const LProgram& prog) {
    auto mod = mlir::ModuleOp::create(loc_);

    // Pass 0: register struct LLVM types.
    for (auto& sd : prog.structs)
        if (!register_struct(sd)) return nullptr;

    // Pass 0a: register class LLVM types (with prepended vtable pointer).
    for (auto& cd : prog.classes)
        if (!register_class(mod, cd)) return nullptr;

    // Pass 0.5: register enum types and module constants.
    for (auto& ed : prog.enums) {
        enum_types_[ed.name] = &ed;
        if (ed.has_payload()) register_tagged_enum(ed);
    }

    for (auto& ta : prog.type_aliases)
        type_aliases_[ta.name] = logos_to_mlir(ta.type);

    for (auto& c : prog.consts)
        module_consts_[c.name] = &c;

    // Declare malloc and free for 'new' and 'delete'.
    ensure_malloc_free(mod);

    // Pass 1: forward-declare all functions (structs, classes, free fns).
    for (auto& sd : prog.structs)
        for (auto& m : sd.methods)
            forward_declare(mod, m);

    for (auto& cd : prog.classes)
        for (auto& m : cd.methods)
            forward_declare(mod, m);

    for (auto& fn : prog.functions)
        forward_declare(mod, fn);

    // Pass 1a: emit vtable globals for concrete classes.
    for (auto& cd : prog.classes)
        if (!cd.is_abstract)
            emit_vtable(mod, cd);

    // Pass 1b: emit vtable globals for trait impls (&dyn Trait support).
    emit_trait_vtables(mod, prog);

    // Pass 2: fill function bodies (structs, classes, free fns).
    for (auto& sd : prog.structs) {
        for (auto& m : sd.methods) {
            auto func = mod.lookupSymbol<mlir::func::FuncOp>(m.name);
            if (!gen_function_body(func, m)) return nullptr;
        }
    }
    for (auto& cd : prog.classes) {
        for (auto& m : cd.methods) {
            auto func = mod.lookupSymbol<mlir::func::FuncOp>(m.name);
            if (!gen_function_body(func, m)) return nullptr;
        }
    }
    for (auto& fn : prog.functions) {
        if (fn.is_extern) continue;
        auto func = mod.lookupSymbol<mlir::func::FuncOp>(fn.name);
        if (!gen_function_body(func, fn)) return nullptr;
    }

    if (mlir::failed(mlir::verify(mod))) {
        std::fprintf(stderr, "mlir_gen: module verification failed\n");
        mod.dump();
        return nullptr;
    }
    return mod;
}

// ---------------------------------------------------------------------------
// Struct helpers
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::get_struct_ptr(const std::string& name) {
    auto it = scope_.find(name);
    if (it == scope_.end()) {
        std::fprintf(stderr, "mlir_gen: undefined '%s'\n", name.c_str());
        return nullptr;
    }
    return it->second;
}

mlir::Value MLIRGenImpl::gep_field(mlir::Value base, const StructInfo& info,
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

// Resolve receiver expr → (object_ptr, type_name).
// Works for both structs (var_struct_) and classes (var_class_).
std::pair<mlir::Value, std::string> MLIRGenImpl::gen_recv_struct(const LExpr& recv) {
    if (auto* vr = std::get_if<EVarRef>(&recv.kind)) {
        auto& name = vr->name;
        // Check class first
        auto cit = var_class_.find(name);
        if (cit != var_class_.end())
            return {get_struct_ptr(name), cit->second};
        // Then struct
        auto sit = var_struct_.find(name);
        if (sit != var_struct_.end())
            return {get_struct_ptr(name), sit->second};
        std::fprintf(stderr, "mlir_gen: '%s' is not a struct/class var\n", name.c_str());
        return {nullptr, {}};
    }
    if (auto* fr = std::get_if<EFieldRead>(&recv.kind)) {
        auto [base_ptr, base_sname] = gen_recv_struct(*fr->receiver);
        if (!base_ptr || base_sname.empty()) return {nullptr, {}};
        auto it = struct_types_.find(base_sname);
        if (it == struct_types_.end()) return {nullptr, {}};
        auto& info = it->second;
        auto gep = gep_field(base_ptr, info, fr->field);
        if (!gep) return {nullptr, {}};
        for (auto& f : info.fields) {
            if (f.name == fr->field) {
                if (!f.struct_name.empty()) {
                    auto obj_ptr = builder_.create<mlir::LLVM::LoadOp>(
                                       loc_, ptr_type(), gep);
                    return {obj_ptr, f.struct_name};
                }
                std::fprintf(stderr, "mlir_gen: field '%s' is not a struct/class type\n",
                             fr->field.c_str());
                return {nullptr, {}};
            }
        }
        return {nullptr, {}};
    }
    // General case: evaluate expression, derive type name from LExpr.type
    auto ptr = gen_expr(recv);
    if (!ptr) return {nullptr, {}};
    // If the result is an aggregate struct (by-value return), spill to alloca.
    if (mlir::isa<mlir::LLVM::LLVMStructType>(ptr.getType()))
        ptr = spill_to_alloca(ptr);
    if (recv.type) {
        const LogosType* t = recv.type;
        // Strip one level of pointer/reference to get the struct/class type
        if ((t->kind == LogosType::Kind::Ptr ||
             t->kind == LogosType::Kind::Ref ||
             t->kind == LogosType::Kind::MutRef) && t->pointee) t = t->pointee;
        if (t->kind == LogosType::Kind::Struct)
            return {ptr, concrete_struct_name(t)};
        if (t->kind == LogosType::Kind::Class)
            return {ptr, concrete_class_name(t)};
    }
    std::fprintf(stderr, "mlir_gen: unsupported receiver kind for struct/class access\n");
    return {nullptr, {}};
}

mlir::Value MLIRGenImpl::gen_struct_lit(const EStructLit& e) {
    auto sit = struct_types_.find(e.name);
    if (sit == struct_types_.end()) {
        std::fprintf(stderr, "mlir_gen: unknown struct '%s'\n", e.name.c_str());
        return nullptr;
    }
    auto& info  = sit->second;
    auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                      loc_, ptr_type(), info.llvm_type, i64_one());
    for (auto& [fname, fval] : e.fields) {
        // Find field metadata.
        const FieldInfo* fi = nullptr;
        for (auto& f : info.fields) if (f.name == fname) { fi = &f; break; }

        auto gep = gep_field(alloca, info, fname);
        if (!gep) return nullptr;

        // Special case: if the field is an inline array (LLVMArrayType) and
        // the initialiser is an EArrLit, copy elements one-by-one into the
        // struct field instead of storing a pointer returned by gen_arr_lit.
        auto arr_llvm = fi ? mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(fi->type)
                           : mlir::LLVM::LLVMArrayType{};
        if (arr_llvm && std::holds_alternative<EArrLit>(fval->kind)) {
            auto& arr_lit = std::get<EArrLit>(fval->kind);
            auto elem_type = arr_llvm.getElementType();
            for (uint64_t i = 0; i < arr_lit.elems.size(); ++i) {
                auto val = gen_expr(*arr_lit.elems[i]);
                if (!val) return nullptr;
                val = coerce_int(val, elem_type);
                llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(i)};
                auto elem_gep = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), arr_llvm, gep, idx);
                builder_.create<mlir::LLVM::StoreOp>(loc_, val, elem_gep);
            }
            continue;
        }

        auto val = gen_expr(*fval);
        if (!val) return nullptr;
        // Coerce scalar literals to the field's declared type (e.g. IntLit→i64).
        if (fi && fi->type && !mlir::isa<mlir::LLVM::LLVMArrayType>(fi->type))
            val = coerce_int(val, fi->type);
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
    }
    return alloca;
}

// ---------------------------------------------------------------------------
// Array helpers
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::get_subscript_ptr(const std::string& name) {
    auto it = scope_.find(name);
    if (it == scope_.end()) {
        std::fprintf(stderr, "mlir_gen: undefined '%s'\n", name.c_str());
        return nullptr;
    }
    return it->second;
}

mlir::Type MLIRGenImpl::subscript_elem_type(const std::string& name) {
    auto it = var_elem_types_.find(name);
    if (it != var_elem_types_.end()) return it->second;
    auto sit = var_subscript_.find(name);
    if (sit != var_subscript_.end()) return sit->second;
    return builder_.getI32Type();
}

mlir::Value MLIRGenImpl::gen_arr_lit(const EArrLit& e, mlir::Type elem_type) {
    uint64_t n = e.elems.size();
    auto arr_type = mlir::LLVM::LLVMArrayType::get(elem_type, n);
    auto alloca   = builder_.create<mlir::LLVM::AllocaOp>(
                        loc_, ptr_type(), arr_type, i64_one());
    bool elem_is_array = elem_type && mlir::isa<mlir::LLVM::LLVMArrayType>(elem_type);
    for (uint64_t i = 0; i < n; ++i) {
        llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(i)};
        auto gep = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), elem_type, alloca, idx);
        if (elem_is_array) {
            // For nested arrays: the sub-element is an EArrLit generating a pointer.
            // We need to copy its contents element-by-element into gep.
            auto inner_ptr = gen_expr(*e.elems[i]);
            if (!inner_ptr) return nullptr;
            // Copy the inner array by loading and storing
            auto inner_arr_type = mlir::cast<mlir::LLVM::LLVMArrayType>(elem_type);
            auto inner_elem_type = inner_arr_type.getElementType();
            for (uint64_t j = 0; j < inner_arr_type.getNumElements(); ++j) {
                llvm::SmallVector<mlir::LLVM::GEPArg> inner_idx{int32_t(j)};
                auto src_gep = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), inner_elem_type, inner_ptr, inner_idx);
                auto val = builder_.create<mlir::LLVM::LoadOp>(loc_, inner_elem_type, src_gep);
                auto dst_gep = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), inner_elem_type, gep, inner_idx);
                builder_.create<mlir::LLVM::StoreOp>(loc_, val, dst_gep);
            }
        } else {
            auto val = gen_expr(*e.elems[i]);
            if (!val) return nullptr;
            val = coerce_int(val, elem_type);
            builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
        }
    }
    return alloca;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

mlir::OwningOpRef<mlir::ModuleOp> mlir_gen(mlir::MLIRContext& ctx,
                                            const LProgram& prog) noexcept
{
    MLIRGenImpl gen(ctx);
    return gen.generate(prog);
}

} // namespace logos::compiler
