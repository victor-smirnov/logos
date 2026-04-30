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
#include <set>
#include <string>
#include <vector>

namespace logos::compiler {

using namespace lir;

// ---------------------------------------------------------------------------
// MLIRGenImpl::generate — top-level lowering pipeline
// ---------------------------------------------------------------------------

mlir::OwningOpRef<mlir::ModuleOp> MLIRGenImpl::generate(const LProgram& prog) {
    auto mod = mlir::ModuleOp::create(loc_);

    prog_   = &prog;
    mirror_ = prog.mirror_table.get();

    // Pass 0: build struct lookup table so register_tagged_enum can compute
    // payload sizes from LogosType field trees (logos_abi_byte_size).
    for (auto& sd : prog.structs)
        all_struct_defs_[sd.name] = &sd;

    // Pass 0.5: register enum types (needs all_struct_defs_ populated above).
    for (auto& ed : prog.enums) {
        enum_types_[ed.name] = &ed;
        if (ed.has_payload()) register_tagged_enum(ed);
    }

    // Register struct LLVM types (all_struct_defs_ already built above).
    for (auto& sd : prog.structs)
        if (!register_struct(sd)) return nullptr;

    for (auto& ta : prog.type_aliases)
        type_aliases_[ta.name] = logos_to_mlir(ta.type);

    for (auto& c : prog.consts)
        module_consts_[c.name] = &c;

    // Declare malloc and free for 'new' and 'delete'.
    ensure_malloc_free(mod);

    // Pass 1: forward-declare all functions (structs, free fns).
    // Skip non-generic functions from binary modules — the linker finds them in the .a.
    // Skip functions already compiled into a binary archive — the linker will
    // find them there.  We check prog.binary_symbols (populated from nm output)
    // rather than the from_binary_module flag, because generic instantiations
    // from binary-module templates are NOT in the archive and must be compiled.
    auto is_binary_skip = [&prog](const lir::LFunction& fn) -> bool {
        if (fn.is_extern || prog.binary_symbols.empty()) return false;
        return prog.binary_symbols.count(fn.name) > 0;
    };

    if (std::getenv("LOGOS_TRACE_PHASES")) {
        size_t total = 0, skipped = 0;
        for (auto& sd : prog.structs) for (auto& m : sd.methods) { ++total; if (is_binary_skip(*m)) ++skipped; }
        for (auto& fn : prog.functions) { ++total; if (is_binary_skip(*fn)) ++skipped; }
        std::fprintf(stderr, "mlir_gen: %zu functions total, %zu binary-skip\n", total, skipped);
    }

    // Always forward-declare every function so call sites can resolve the
    // signature.  Binary-skip only suppresses body emission — the linker
    // provides the implementation from the archive.
    for (auto& sd : prog.structs)
        for (auto& m : sd.methods)
            forward_declare(mod, *m);

    for (auto& fn : prog.functions)
        forward_declare(mod, *fn);

    // Binary-skip functions are declarations only (no body); MLIR requires
    // declarations to have private visibility.
    for (auto& sd : prog.structs)
        for (auto& m : sd.methods)
            if (is_binary_skip(*m))
                if (auto f = mod.lookupSymbol<mlir::func::FuncOp>(m->name))
                    f.setPrivate();
    for (auto& fn : prog.functions)
        if (is_binary_skip(*fn))
            if (auto f = mod.lookupSymbol<mlir::func::FuncOp>(fn->name))
                f.setPrivate();

    // Pass 1b: emit vtable globals for trait impls (&dyn Trait support).
    emit_trait_vtables(mod, prog);

    // Pass 1c: emit tag-based dispatch tables (one [223 x ptr] per TagSystem×Trait×method).
    emit_tag_dispatch_tables(mod, prog);

    // Pass 1d: emit TypeInfo rodata globals for reflect::<T>() and annotated types.
    {
        auto i8 = builder_.getIntegerType(8);
        builder_.setInsertionPointToEnd(mod.getBody());
        for (auto& rg : prog.reflection_globals) {
            // Avoid duplicate emission (e.g. stdlib pre-compiled + current TU).
            if (mod.lookupSymbol(rg.symbol)) continue;
            auto arr_type = mlir::LLVM::LLVMArrayType::get(i8, rg.blob.size());
            auto blob_attr = builder_.getStringAttr(
                llvm::StringRef(reinterpret_cast<const char*>(rg.blob.data()), rg.blob.size()));
            // WeakODR: multiple TUs can emit the same symbol; linker keeps one.
            builder_.create<mlir::LLVM::GlobalOp>(
                loc_, arr_type, /*isConstant=*/true, mlir::LLVM::Linkage::WeakODR,
                rg.symbol, blob_attr);
        }
    }

    // Pass 2: fill function bodies (structs, free fns).
    for (auto& sd : prog.structs) {
        for (auto& m : sd.methods) {
            if (is_binary_skip(*m)) continue;
            auto func = mod.lookupSymbol<mlir::func::FuncOp>(m->name);
            if (!gen_function_body(func, *m)) return nullptr;
        }
    }
    for (auto& fn : prog.functions) {
        if (fn->is_extern || is_binary_skip(*fn)) continue;
        auto func = mod.lookupSymbol<mlir::func::FuncOp>(fn->name);
        if (!gen_function_body(func, *fn)) return nullptr;
    }

    // Pass 3: inject dispatch table init calls at the start of main().
    // Each tag system has __logos_tag_dispatch_init__<TagSystem>.  Binary tag
    // systems have this function in the archive; non-binary systems have it in
    // the MLIR module (emitted by emit_tag_dispatch_tables).  We forward-declare
    // and call all of them so the linker resolves the binary ones from the .a.
    if (!prog.dispatch_entries.empty()) {
        auto main_fn = mod.lookupSymbol<mlir::func::FuncOp>("main");
        if (main_fn && !main_fn.empty()) {
            mlir::OpBuilder::InsertionGuard guard(builder_);
            // Collect unique tag systems in stable order.
            std::set<std::string> seen_systems;
            std::vector<std::string> init_fns;
            for (auto& de : prog.dispatch_entries) {
                if (de.tag_system.empty()) continue;
                if (!seen_systems.insert(de.tag_system).second) continue;
                init_fns.push_back("__logos_tag_dispatch_init__" + de.tag_system);
            }
            // Forward-declare any init fn not yet in the module (binary archive provides it).
            auto void_fn_type = mlir::FunctionType::get(builder_.getContext(), {}, {});
            for (auto& fn_name : init_fns) {
                if (!mod.lookupSymbol<mlir::func::FuncOp>(fn_name)) {
                    builder_.setInsertionPointToEnd(mod.getBody());
                    auto decl = builder_.create<mlir::func::FuncOp>(loc_, fn_name, void_fn_type);
                    decl.setPrivate();
                }
            }
            // Inject calls at the start of main.
            builder_.setInsertionPointToStart(&main_fn.front());
            for (auto& fn_name : init_fns) {
                auto init_fn = mod.lookupSymbol<mlir::func::FuncOp>(fn_name);
                builder_.create<mlir::func::CallOp>(loc_, init_fn, mlir::ValueRange{});
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

// ---------------------------------------------------------------------------
// Struct helpers
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::get_struct_ptr(const std::string& name) {
    auto it = scope_.find(name);
    if (it == scope_.end()) {
        std::fprintf(stderr, "mlir_gen: undefined '%s'\n", name.c_str());
        return nullptr;
    }
    // Mutable raw-pointer locals are stored as alloca(ptr) slots.
    // For struct receivers we need the pointee pointer value, not the slot address.
    if (var_local_ptrs_.count(name) && let_vars_.count(name))
        return builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), it->second);
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
    namespace ec = lir_schema::expr;
    auto recv_ref = expr_ref_of(recv);
    auto recv_kind = recv_ref ? recv_ref.kind() : ec::Code(0);
    if (recv_kind == ec::Code::VarRef) {
        std::string name(lir_view::EVarRefView{recv_ref}.name());
        // Check class first
        auto cit = var_class_.find(name);
        if (cit != var_class_.end())
            return {get_struct_ptr(name), cit->second};
        // Then struct
        auto sit = var_struct_.find(name);
        if (sit != var_struct_.end())
            return {get_struct_ptr(name), sit->second};
        // Local pointer-to-struct/datatype slots already record the concrete
        // LLVM aggregate type.  Use that as a fallback even if recv.type was
        // not preserved through lowering.
        auto lpit = var_local_ptrs_.find(name);
        if (lpit != var_local_ptrs_.end()) {
            if (auto sc = scope_.find(name); sc != scope_.end()) {
                if (recv.type && TypeRef(recv.type).kind() == LogosType::Kind::Ptr &&
                    TypeRef(recv.type).pointee() &&
                    (TypeRef(recv.type).pointee().kind() == LogosType::Kind::Struct ||
                     TypeRef(recv.type).pointee().kind() == LogosType::Kind::ZonedStruct)) {
                    return {sc->second, concrete_struct_name(TypeRef(recv.type).pointee())};
                }
                // If the receiver type is unavailable, still treat it as a
                // struct/datatype pointer using the recorded aggregate type.
                for (auto& [sname, info] : struct_types_) {
                    if (info.llvm_type == lpit->second)
                        return {sc->second, sname};
                }
            }
        }
        // Last resort: if the receiver is a method parameter/let binding that
        // already lives in scope as a pointer value, trust the caller-side
        // lowering and return it directly.  This covers `self: *const T` and
        // `self: &T` receivers even when the LIR type annotation got dropped.
        if (auto sc = scope_.find(name); sc != scope_.end()) {
            if (recv.type) {
                TypeRef tv{recv.type};
                if ((tv.kind() == LogosType::Kind::Ptr ||
                     tv.kind() == LogosType::Kind::Ref ||
                     tv.kind() == LogosType::Kind::MutRef) && tv.pointee() &&
                    (tv.pointee().kind() == LogosType::Kind::Struct ||
                     tv.pointee().kind() == LogosType::Kind::ZonedStruct)) {
                    return {sc->second, concrete_struct_name(tv.pointee())};
                }
            }
            // If this binding is a pointer slot (alloca(ptr)), load the value
            // directly and let later field/method code decide how to use it.
            if (var_local_ptrs_.count(name)) {
                auto ptr_val = builder_.create<mlir::LLVM::LoadOp>(
                    loc_, ptr_type(), sc->second);
                auto it2 = std::find_if(struct_types_.begin(), struct_types_.end(),
                    [&](const auto& kv) { return kv.second.llvm_type == var_local_ptrs_[name]; });
                if (it2 != struct_types_.end())
                    return {ptr_val, it2->first};
                return {ptr_val, {}};
            }
        }
        // Check if this is a pointer-to-struct variable (e.g. *mut Point).
        // The logical type is Ptr/Ref/MutRef with pointee=Struct/Class.
        if (recv.type) {
            TypeRef tv{recv.type};
            if (type_str(recv.type) == "AnyVal") {
                auto sc = scope_.find(name);
                if (sc != scope_.end())
                    return {sc->second, "AnyVal"};
            }
            if ((tv.kind() == LogosType::Kind::Ptr ||
                 tv.kind() == LogosType::Kind::Ref ||
                 tv.kind() == LogosType::Kind::MutRef) && tv.pointee()) {
                TypeRef inner = tv.pointee();
                if (type_str(inner) == "AnyVal") {
                    auto sc = scope_.find(name);
                    if (sc != scope_.end())
                        return {sc->second, "AnyVal"};
                }
                if (TypeRef(inner).kind() == LogosType::Kind::Struct ||
                    TypeRef(inner).kind() == LogosType::Kind::ZonedStruct) {
                    auto sc = scope_.find(name);
                    if (sc != scope_.end()) {
                        // Local let-bound pointer variables are stored in an alloca(slot).
                        // Parameters / SSA values already are the pointer value.
                        auto lpit = var_local_ptrs_.find(name);
                        if (lpit != var_local_ptrs_.end()) {
                            auto ptr_val = builder_.create<mlir::LLVM::LoadOp>(
                                loc_, ptr_type(), sc->second);
                            return {ptr_val, concrete_struct_name(inner)};
                        }
                        return {sc->second, concrete_struct_name(inner)};
                    }
                }
            }
        }
        // Fall through to the general receiver path instead of hard-failing.
        // Some pointer-like receivers are lowered as plain values in scope_
        // or through temp SSA values, and gen_expr(recv) can still recover them.
    }
    if (recv_kind == ec::Code::FieldRead) {
        lir_view::EFieldReadView fr{recv_ref};
        auto rec_le = lexpr_of(fr.receiver());
        if (!rec_le) return {nullptr, {}};
        std::string field(fr.field());
        auto [base_ptr, base_sname] = gen_recv_struct(*rec_le);
        if (!base_ptr || base_sname.empty()) return {nullptr, {}};
        auto it = struct_types_.find(base_sname);
        if (it == struct_types_.end()) return {nullptr, {}};
        auto& info = it->second;
        auto gep = gep_field(base_ptr, info, field);
        if (!gep) return {nullptr, {}};
        for (auto& f : info.fields) {
            if (f.name == field) {
                if (!f.struct_name.empty()) {
                    // Check if field is inline-embedded struct (LLVMStructType) or pointer.
                    if (mlir::isa<mlir::LLVM::LLVMStructType>(f.type)) {
                        // Inline embed: GEP already points to the field
                        // in-place — return it directly so &mut self methods
                        // mutate the original, not a copy.
                        return {gep, f.struct_name};
                    }
                    // Pointer field: load the pointer.
                    auto obj_ptr = builder_.create<mlir::LLVM::LoadOp>(
                                       loc_, ptr_type(), gep);
                    return {obj_ptr, f.struct_name};
                }
                std::fprintf(stderr, "mlir_gen: field '%s' is not a struct/class type\n",
                             field.c_str());
                return {nullptr, {}};
            }
        }
        return {nullptr, {}};
    }
    // General case: evaluate expression, derive type name from LExpr.type
    auto ptr = gen_expr(recv);
    if (!ptr) return {nullptr, {}};
    // If the result is an aggregate struct (by-value return), spill to alloca.
    // AnyVal is a scalar-like 4-byte slot, not a by-value aggregate receiver.
    if (mlir::isa<mlir::LLVM::LLVMStructType>(ptr.getType()) &&
        (!recv.type || type_str(recv.type) != "AnyVal"))
        ptr = spill_to_alloca(ptr);
    if (recv.type) {
        TypeRef t = recv.type;
        // Strip one level of pointer/reference to get the struct/class type
        TypeRef tv{t};
        if ((tv.kind() == LogosType::Kind::Ptr ||
             tv.kind() == LogosType::Kind::Ref ||
             tv.kind() == LogosType::Kind::MutRef) && tv.pointee()) t = tv.pointee();
        tv = TypeRef{t};
        if (tv.kind() == LogosType::Kind::Struct ||
            tv.kind() == LogosType::Kind::ZonedStruct)
            return {ptr, concrete_struct_name(t)};
    }
    std::fprintf(stderr, "mlir_gen: unsupported receiver kind for struct/class access\n");
    return {nullptr, {}};
}

mlir::Value MLIRGenImpl::gen_struct_lit(lir_view::EStructLitView v) {
    namespace ec = lir_schema::expr;
    std::string name(v.name());
    if (name == "AnyVal") {
        // AnyVal is lowered as a scalar i32 everywhere in MLIR.
        // Hermes source still spells it as a struct literal (`AnyVal { raw: ... }`),
        // so treat that syntax as a constructor for the raw slot value.
        std::vector<std::pair<std::string, lir_view::ExprRef>> fields;
        v.each_field([&](std::string_view fn, lir_view::ExprRef val){
            fields.emplace_back(std::string(fn), val);
        });
        if (fields.size() != 1 || fields.front().first != "raw") {
            std::fprintf(stderr, "mlir_gen: AnyVal literal expects a single 'raw' field\n");
            return nullptr;
        }
        auto* le = lexpr_of(fields.front().second); if (!le) return nullptr;
        auto raw = gen_expr(*le);
        if (!raw) return nullptr;
        return coerce_numeric(raw, builder_.getI32Type());
    }
    auto sit = struct_types_.find(name);
    if (sit == struct_types_.end()) {
        std::fprintf(stderr, "mlir_gen: unknown struct '%s'\n", name.c_str());
        return nullptr;
    }
    auto& info  = sit->second;
    auto alloca = create_entry_alloca(info.llvm_type);
    bool ok = true;
    v.each_field([&](std::string_view fn_sv, lir_view::ExprRef fval){
        if (!ok) return;
        std::string fname(fn_sv);
        // Find field metadata.
        const FieldInfo* fi = nullptr;
        for (auto& f : info.fields) if (f.name == fname) { fi = &f; break; }

        auto gep = gep_field(alloca, info, fname);
        if (!gep) { ok = false; return; }

        // Special case: if the field is an inline array (LLVMArrayType) and
        // the initialiser is an EArrLit, copy elements one-by-one into the
        // struct field instead of storing a pointer returned by gen_arr_lit.
        auto arr_llvm = fi ? mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(fi->type)
                           : mlir::LLVM::LLVMArrayType{};
        if (arr_llvm && fval.kind() == ec::Code::ArrLit) {
            lir_view::EArrLitView arr_view{fval};
            auto elem_type = arr_llvm.getElementType();
            uint64_t n = arr_view.count();
            for (uint64_t i = 0; i < n; ++i) {
                auto er = arr_view.elem(i);
                auto* le = lexpr_of(er); if (!le) { ok = false; return; }
                auto val = gen_expr(*le);
                if (!val) { ok = false; return; }
                val = coerce_numeric(val, elem_type);
                llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(i)};
                auto elem_gep = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), arr_llvm, gep, idx);
                builder_.create<mlir::LLVM::StoreOp>(loc_, val, elem_gep);
            }
            return;
        }

        auto* fle = lexpr_of(fval); if (!fle) { ok = false; return; }
        auto val = gen_expr(*fle);
        if (!val) { ok = false; return; }
        // Coerce scalar literals to the field's declared type (e.g. IntLit→i64, FloatLit→f32).
        if (fi && fi->type && !mlir::isa<mlir::LLVM::LLVMArrayType>(fi->type)) {
            if (mlir::isa<mlir::LLVM::LLVMStructType>(fi->type) &&
                val.getType() == ptr_type()) {
                // Inline struct field: load the aggregate value from the alloca pointer.
                val = builder_.create<mlir::LLVM::LoadOp>(loc_, fi->type, val);
            } else {
                val = coerce_numeric(val, fi->type);
            }
        }
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
    });
    return ok ? alloca : nullptr;
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

mlir::Value MLIRGenImpl::gen_arr_lit(lir_view::EArrLitView v, mlir::Type elem_type) {
    uint64_t n = v.count();
    auto arr_type = mlir::LLVM::LLVMArrayType::get(elem_type, n);
    auto alloca   = create_entry_alloca(arr_type);
    bool elem_is_array  = elem_type && mlir::isa<mlir::LLVM::LLVMArrayType>(elem_type);
    bool elem_is_struct = elem_type && mlir::isa<mlir::LLVM::LLVMStructType>(elem_type);
    for (uint64_t i = 0; i < n; ++i) {
        llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(i)};
        auto gep = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), arr_type, alloca, idx);
        auto er = v.elem(i);
        auto* le = lexpr_of(er); if (!le) return nullptr;
        if (elem_is_struct) {
            // Element is an inline LLVM struct slot. gen_expr may return
            // either a pointer to the source struct (alloca/struct_lit) or
            // the struct value itself (function return). Either way, write
            // sizeof(struct) bytes into the slot so the *value* lives in
            // the array — this is what makes returning [Struct;N] safe.
            auto src = gen_expr(*le);
            if (!src) return nullptr;
            if (src.getType() == ptr_type()) {
                auto dl = mlir::DataLayout::closest(builder_.getInsertionBlock()->getParentOp());
                auto bytes = (int64_t)dl.getTypeSize(elem_type);
                auto sz = builder_.create<mlir::LLVM::ConstantOp>(
                    loc_, builder_.getI64Type(),
                    builder_.getI64IntegerAttr(bytes));
                builder_.create<mlir::LLVM::MemcpyOp>(
                    loc_, gep, src, sz, /*isVolatile=*/false);
            } else {
                builder_.create<mlir::LLVM::StoreOp>(loc_, src, gep);
            }
            continue;
        }
        if (elem_is_array) {
            auto inner_ptr = gen_expr(*le);
            if (!inner_ptr) return nullptr;
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
            auto val = gen_expr(*le);
            if (!val) return nullptr;
            val = coerce_numeric(val, elem_type);
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
